from types import SimpleNamespace

import pytest

from adjust import MachineConfig, MachineRuntime, MetricsState
from plc_config import MqttSecurityConfig, env_bool


class FakeMqttClient:
    def __init__(self) -> None:
        self.username_call = None
        self.tls_call = None
        self.insecure_call = None

    def username_pw_set(self, username, password):
        self.username_call = (username, password)

    def tls_set(self, **kwargs):
        self.tls_call = kwargs

    def tls_insecure_set(self, value):
        self.insecure_call = value


def test_mqtt_security_configures_authenticated_verified_tls(tmp_path):
    password_file = tmp_path / "mqtt-password"
    ca_file = tmp_path / "factory-ca.pem"
    cert_file = tmp_path / "client.pem"
    key_file = tmp_path / "client-key.pem"
    password_file.write_text("secret-value\n", encoding="utf-8")
    for path in (ca_file, cert_file, key_file):
        path.write_text("test material", encoding="utf-8")
    config = MqttSecurityConfig(
        username="edge-client",
        password_file=str(password_file),
        tls=True,
        ca_file=str(ca_file),
        cert_file=str(cert_file),
        key_file=str(key_file),
    )
    client = FakeMqttClient()

    config.configure_client(client)

    assert client.username_call == ("edge-client", "secret-value")
    assert client.tls_call["ca_certs"] == str(ca_file)
    assert client.tls_call["certfile"] == str(cert_file)
    assert client.tls_call["keyfile"] == str(key_file)
    assert client.insecure_call is False


@pytest.mark.parametrize(
    "config, message",
    [
        (MqttSecurityConfig(password_file="password"), "username is required"),
        (MqttSecurityConfig(username="edge-client"), "TLS must be enabled"),
        (MqttSecurityConfig(tls=True, cert_file="cert"), "configured together"),
        (MqttSecurityConfig(ca_file="ca"), "TLS must be enabled"),
        (MqttSecurityConfig(tls_insecure=True), "TLS must be enabled"),
        (MqttSecurityConfig(require_secure=True), "requires TLS"),
        (MqttSecurityConfig(tls=True, require_secure=True), "requires username authentication"),
        (
            MqttSecurityConfig(tls=True, username="edge-client", tls_insecure=True, require_secure=True),
            "forbids disabling",
        ),
    ],
)
def test_mqtt_security_rejects_unsafe_combinations(config, message):
    with pytest.raises(ValueError, match=message):
        config.validate(check_files=False)


def test_mqtt_password_is_not_put_in_decoder_command_or_environment(tmp_path):
    password_file = tmp_path / "mqtt-password"
    password_file.write_text("top-secret", encoding="utf-8")
    args = SimpleNamespace(
        decoder_broker="mqtt.factory.example",
        decoder_port=8883,
        decoder_processor="lineprotocol",
        decoder_sqlite_path=str(tmp_path / "points.db"),
        decoder_dlq_path=str(tmp_path / "dlq.db"),
        mqtt_username="edge-client",
        mqtt_password_file=str(password_file),
        mqtt_tls=True,
        mqtt_ca_file=None,
        mqtt_cert_file=None,
        mqtt_key_file=None,
        mqtt_tls_insecure=False,
        require_secure_mqtt=True,
    )
    runtime = MachineRuntime(MachineConfig("FAB01", "ttyACM0"), args, MetricsState())

    command = runtime._decoder_command("FAB01/#")
    environment = runtime._decoder_environment()

    assert "top-secret" not in command
    assert "top-secret" not in environment.values()
    assert environment["PLC_MQTT_PASSWORD_FILE"] == str(password_file)


def test_machine_config_rejects_probe_or_topic_injection():
    with pytest.raises(ValueError, match="machine_sn"):
        MachineConfig("FAB01/#", "ttyACM0")
    with pytest.raises(ValueError, match="serial_port"):
        MachineConfig("FAB01", "ttyACM0'; return 0;")
    with pytest.raises(ValueError, match="must not exceed 256"):
        MachineConfig("FAB01", "ttyACM0", max_module=17, max_unit=16)


def test_env_bool_is_strict(monkeypatch):
    monkeypatch.setenv("PLC_TEST_BOOL", "definitely")
    with pytest.raises(ValueError, match="must be one of"):
        env_bool("PLC_TEST_BOOL")
