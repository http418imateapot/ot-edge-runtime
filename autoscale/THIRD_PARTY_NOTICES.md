# Third-party notices

This repository is distributed under the MIT License. It depends on software that is distributed separately under its own license terms. No third-party source code is vendored in this repository.

## Runtime dependencies

| Component | Role | Upstream license | Upstream project |
|---|---|---|---|
| Eclipse Paho MQTT for Python (`paho-mqtt`) | MQTT client | EPL-2.0 OR EDL-1.0 | https://github.com/eclipse-paho/paho.mqtt.python |
| PyYAML | YAML configuration parser | MIT | https://github.com/yaml/pyyaml |
| BCC | Host-provided eBPF compiler and Python bindings | Apache-2.0 | https://github.com/iovisor/bcc |

## Development and packaging dependencies

| Component | Role | Upstream license | Upstream project |
|---|---|---|---|
| pytest | Test runner | MIT | https://github.com/pytest-dev/pytest |
| build | Python package build frontend | MIT | https://github.com/pypa/build |
| setuptools | Python build backend | MIT | https://github.com/pypa/setuptools |
| wheel | Python wheel support | MIT | https://github.com/pypa/wheel |

Operators and redistributors are responsible for reviewing the exact versions installed in their environment and retaining all notices required by those upstream licenses. This file is an inventory, not legal advice.
