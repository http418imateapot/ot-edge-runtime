"""Shared production configuration helpers for PLC EdgeFlow services."""

from __future__ import annotations

import os
import ssl
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping


TRUE_VALUES = {"1", "true", "yes", "on"}
FALSE_VALUES = {"0", "false", "no", "off"}


def env_text(name: str, default: str | None = None) -> str | None:
    value = os.getenv(name)
    if value is None:
        return default
    value = value.strip()
    return value if value else default


def env_int(name: str, default: int) -> int:
    value = env_text(name)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError as exc:
        raise ValueError(f"{name} must be an integer") from exc


def env_bool(name: str, default: bool = False) -> bool:
    value = env_text(name)
    if value is None:
        return default
    normalized = value.lower()
    if normalized in TRUE_VALUES:
        return True
    if normalized in FALSE_VALUES:
        return False
    raise ValueError(f"{name} must be one of: true, false, 1, 0, yes, no, on, off")


@dataclass(frozen=True)
class MqttSecurityConfig:
    """MQTT authentication and TLS settings that are safe to share across services."""

    username: str | None = None
    password_file: str | None = None
    tls: bool = False
    ca_file: str | None = None
    cert_file: str | None = None
    key_file: str | None = None
    tls_insecure: bool = False
    require_secure: bool = False

    @classmethod
    def from_namespace(cls, args: Any) -> "MqttSecurityConfig":
        return cls(
            username=getattr(args, "mqtt_username", None),
            password_file=getattr(args, "mqtt_password_file", None),
            tls=bool(getattr(args, "mqtt_tls", False)),
            ca_file=getattr(args, "mqtt_ca_file", None),
            cert_file=getattr(args, "mqtt_cert_file", None),
            key_file=getattr(args, "mqtt_key_file", None),
            tls_insecure=bool(getattr(args, "mqtt_tls_insecure", False)),
            require_secure=bool(getattr(args, "require_secure_mqtt", False)),
        )

    def validate(self, *, check_files: bool = True) -> None:
        if self.password_file and not self.username:
            raise ValueError("MQTT username is required when a password file is configured")
        if self.username and not self.tls:
            raise ValueError("TLS must be enabled when MQTT username authentication is configured")
        if bool(self.cert_file) != bool(self.key_file):
            raise ValueError("MQTT client certificate and key files must be configured together")
        tls_only_values = {
            "MQTT CA file": self.ca_file,
            "MQTT client certificate": self.cert_file,
            "MQTT client key": self.key_file,
        }
        configured_tls_only = [name for name, value in tls_only_values.items() if value]
        if configured_tls_only and not self.tls:
            raise ValueError(f"TLS must be enabled when configuring {', '.join(configured_tls_only)}")
        if self.tls_insecure and not self.tls:
            raise ValueError("TLS must be enabled before certificate verification can be disabled")
        if self.require_secure:
            if not self.tls:
                raise ValueError("secure MQTT mode requires TLS")
            if self.tls_insecure:
                raise ValueError("secure MQTT mode forbids disabling broker hostname verification")
            if not self.username and not self.cert_file:
                raise ValueError("secure MQTT mode requires username authentication or a client certificate")
        if check_files:
            for label, value in {
                "MQTT password file": self.password_file,
                **tls_only_values,
            }.items():
                if value and not Path(value).is_file():
                    raise ValueError(f"{label} does not exist or is not a regular file: {value}")

    def read_password(self) -> str | None:
        if not self.password_file:
            return None
        if Path(self.password_file).stat().st_size > 65_536:
            raise ValueError("MQTT password file must not exceed 65536 bytes")
        password = Path(self.password_file).read_text(encoding="utf-8").rstrip("\r\n")
        if not password:
            raise ValueError("MQTT password file is empty")
        return password

    def configure_client(self, client: Any) -> None:
        self.validate()
        if self.username:
            client.username_pw_set(self.username, self.read_password())
        if self.tls:
            client.tls_set(
                ca_certs=self.ca_file,
                certfile=self.cert_file,
                keyfile=self.key_file,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
            client.tls_insecure_set(self.tls_insecure)

    def as_environment(self, base: Mapping[str, str] | None = None) -> dict[str, str]:
        environment = dict(base or {})
        values = {
            "PLC_MQTT_USERNAME": self.username,
            "PLC_MQTT_PASSWORD_FILE": self.password_file,
            "PLC_MQTT_TLS": "true" if self.tls else "false",
            "PLC_MQTT_CA_FILE": self.ca_file,
            "PLC_MQTT_CERT_FILE": self.cert_file,
            "PLC_MQTT_KEY_FILE": self.key_file,
            "PLC_MQTT_TLS_INSECURE": "true" if self.tls_insecure else "false",
            "PLC_REQUIRE_SECURE_MQTT": "true" if self.require_secure else "false",
        }
        for name, value in values.items():
            if value is None:
                environment.pop(name, None)
            else:
                environment[name] = value
        return environment


def add_mqtt_security_arguments(parser: Any) -> None:
    parser.add_argument(
        "--mqtt-username",
        default=env_text("PLC_MQTT_USERNAME"),
        help="MQTT username; prefer PLC_MQTT_USERNAME in the protected environment file.",
    )
    parser.add_argument(
        "--mqtt-password-file",
        default=env_text("PLC_MQTT_PASSWORD_FILE"),
        help="Path to a UTF-8 file containing the MQTT password. Passwords are never accepted on the command line.",
    )
    parser.add_argument(
        "--mqtt-tls",
        action="store_true",
        default=env_bool("PLC_MQTT_TLS", False),
        help="Enable MQTT TLS with certificate verification.",
    )
    parser.add_argument("--mqtt-ca-file", default=env_text("PLC_MQTT_CA_FILE"), help="PEM CA bundle used to verify the MQTT broker.")
    parser.add_argument("--mqtt-cert-file", default=env_text("PLC_MQTT_CERT_FILE"), help="PEM client certificate for mutual TLS.")
    parser.add_argument("--mqtt-key-file", default=env_text("PLC_MQTT_KEY_FILE"), help="PEM private key for mutual TLS.")
    parser.add_argument(
        "--mqtt-tls-insecure",
        action="store_true",
        default=env_bool("PLC_MQTT_TLS_INSECURE", False),
        help="Disable broker hostname verification. Intended only for isolated commissioning tests.",
    )
    parser.add_argument(
        "--require-secure-mqtt",
        action="store_true",
        default=env_bool("PLC_REQUIRE_SECURE_MQTT", False),
        help="Require verified TLS plus username authentication or mutual TLS.",
    )
