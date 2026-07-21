from pathlib import Path

from decoder import build_parser
from plc_config import MqttSecurityConfig


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def test_adjuster_systemd_unit_enforces_direct_secure_least_privilege_start():
    unit = (PROJECT_ROOT / "systemd" / "plc-adjust.service").read_text(encoding="utf-8")

    assert "ExecStart=/opt/plc-edgeflow/venv/bin/plc-adjust" in unit
    assert "EnvironmentFile=/etc/plc-edgeflow/adjust.env" in unit
    assert "Environment=PLC_REQUIRE_SECURE_MQTT=true" in unit
    assert "CapabilityBoundingSet=CAP_BPF CAP_PERFMON" in unit
    assert "/bin/sh" not in unit
    assert "CAP_SYS_ADMIN" not in unit


def test_production_environment_example_is_valid_secure_mqtt(monkeypatch):
    example = (PROJECT_ROOT / "config" / "adjust.env.example").read_text(encoding="utf-8")
    for raw_line in example.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, value = line.split("=", 1)
        monkeypatch.setenv(name, value)

    args = build_parser().parse_args([])
    security = MqttSecurityConfig.from_namespace(args)

    security.validate(check_files=False)
    assert security.require_secure is True
    assert security.tls is True
    assert security.tls_insecure is False
    assert security.password_file == "/etc/plc-edgeflow/secrets/mqtt-password"
    assert "secret-value" not in example
