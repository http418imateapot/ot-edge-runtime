# eBPF 嵌入式 PLC 點位採集器流量管控

[![CI](https://github.com/http418imateapot/plc-ebpf-autoscaler/actions/workflows/ci.yml/badge.svg)](https://github.com/http418imateapot/plc-ebpf-autoscaler/actions/workflows/ci.yml)
[![Release Pipeline](https://github.com/http418imateapot/plc-ebpf-autoscaler/actions/workflows/release.yml/badge.svg)](https://github.com/http418imateapot/plc-ebpf-autoscaler/actions/workflows/release.yml)
[![GitHub release](https://img.shields.io/github/v/release/http418imateapot/plc-ebpf-autoscaler)](https://github.com/http418imateapot/plc-ebpf-autoscaler/releases)
[![Python](https://img.shields.io/badge/python-3.10%2B-blue)](https://www.python.org/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](../LICENSE)

---

## 開源與商用定位

本專案採 [Apache License 2.0](../LICENSE)，可供商業使用、修改、散布與私有部署，但須保留授權聲明。正式產線導入請依 [Production deployment guide](docs/PRODUCTION_DEPLOYMENT.md) 完成 TLS、最小權限、容量、故障演練、rollout 與 rollback 檢核，並查閱 [Third-party notices](THIRD_PARTY_NOTICES.md)。

本專案是監控與資料管線元件，不是安全儀控、緊急停止或閉迴路機台控制元件，也不宣稱已取得 IEC 62443、IEC 61508、ISO 13849 或場域專屬認證。Apache-2.0 授權允許商用，不等同於提供保固、賠償、SLA 或免除導入單位的驗證責任。

---

## 簡介

### 問題背景

本專案針對半導體高頻量測製程中，以單板電腦開發的 PLC 採集轉拋模組，在訊號採集後產生的資料擁塞問題進行改善。利用 eBPF 在 Linux 核心層即時監控數據流量 (PLC 點位採集資料來自串列埠，並轉發緩存至 MQTT topic)，與採用 Sidecar 架構整合既有系統。當監控到流量超標時，即動態調整點位資料消化程式的實例數量、MQTT Topic 訂閱策略，以實現資源的自動調配。

### 實作方案

* **即時流量監控**：使用 eBPF kretprobe 監控 `tty_read` 實際回傳的 **位元組數**，精確反映真實資料量。
* **動態資源調控**：依位元組流量門檻，調控點位消化程式的實例數量及 MQTT Topic 訂閱策略（支援擴容與縮容）。
* **橫向擴展**：不修改現有點位資料消化程式，以 Sidecar 架構實現動態擴展。
* **穩定部署**：提供 systemd 服務單元，支援開機自啟、崩潰自復原、最小權限執行。

### 專案架構

```
plc-ebpf-autoscaler/
├── README.md                      # 本文件
├── CHANGELOG.md                   # 版本異動紀錄
├── CONTRIBUTING.md                # 貢獻指南
├── SECURITY.md                    # 安全漏洞回報政策
├── pyproject.toml                 # 套件定義（PEP 517/518）
├── requirements.txt               # 鎖定版本依賴（pip install -r 用）
├── decoder.py                     # PLC 採集點位資料消化、解碼程式
├── adjust.py                      # PLC 點位採集資訊量監測、點位消化調控程式
├── tests/
│   ├── test_adjust.py
│   └── test_decoder.py
└── systemd/
    ├── plc-adjust.service         # systemd unit (adjust.py)
    └── plc-decoder@.service       # systemd template unit (decoder.py)
```

---

## 系統需求與安裝

## 專案語言與腳本分析

* **Python 3.10+（核心程式）**
  * `adjust.py`：eBPF 流量監控與 decoder 調度。
  * `decoder.py`：MQTT 訂閱、資料解碼與落地處理。
  * `tests/*.py`：pytest 測試。
* **YAML（系統與流程腳本）**
  * `.github/workflows/*.yml`：CI / Release pipeline。
  * `systemd/*.service`：服務部署設定。
* **Shell 指令（部署與維運）**
  * README 內提供安裝、部署、診斷指令流程。
* **SQL（資料處理相關）**
  * 專案透過 Python 內建 `sqlite3` 模組支援 SQLite 處理模式（`--processor sqlite`），SQL 語句直接嵌入 Python 程式碼中，無獨立 `.sql` 檔案。

---

### 系統需求

* **作業系統**：Linux (Kernel 4.18+，建議 5.8+ 以支援 `CAP_PERFMON`)
* **MQTT Broker**：Mosquitto 或其他相容的 MQTT broker
* **開發工具**
    * Python 3.10+、pip
    * BCC 0.29.1 (用於 eBPF 程式開發)

### 方法 A：pip 安裝（推薦）

> **注意：** BCC 為系統套件，需透過 apt 安裝，不可用 pip 取代。

#### 1. 安裝系統依賴

```bash
sudo apt-get update
sudo apt-get install -y python3 python3-pip mosquitto \
    python3-bpfcc bpfcc-tools linux-headers-$(uname -r)
sudo systemctl enable --now mosquitto
```

#### 2. 安裝本套件

```bash
# 安裝已審查的 release wheel（正式部署請依 production guide 建立專用 venv）
pip install ./plc_ebpf_autoscaler-<version>-py3-none-any.whl

# 或從本機原始碼安裝（開發模式）
pip install -r requirements-dev.txt
```

安裝後即可使用 `plc-adjust` 與 `plc-decoder` 命令：

```bash
plc-adjust --help
plc-decoder --help
```

#### 3. 建立資料目錄

```bash
sudo mkdir -p /var/lib/plc-edgeflow
```

---

### 方法 B：手動安裝（無 pip 環境）

#### 安裝並啟用 MQTT broker (Mosquitto)

```bash
sudo apt-get install mosquitto
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

#### 更新並安裝 Python 3 與 pip

```bash
sudo apt-get update
sudo apt-get install python3 python3-pip
```

#### 安裝 BCC 工具及相關 header

```bash
sudo apt-get install bpfcc-tools linux-headers-$(uname -r)
```

#### 安裝 Python 相依套件

```bash
pip3 install -r requirements.txt
mkdir -p /var/lib/plc-edgeflow
```

---

## 專案情境說明

### 點位採集頻率與資料量

本專案預設情境：每一機台有 16 個模組 × 8 個單元 × 4 個點位（共 512 點），每點 16 bytes，總計 **8192 bytes/s** 的採集數據。點位採集透過單板電腦的 COM / Serial Port 通訊。

### MQTT Topic 結構

```
{machine_sn}/{module_id}/{unit_id}/{point_id}
```

`adjust.py` 依流量分三個層級訂閱（均符合 MQTT 3.1.1 規範）：

| 流量層級 | 訂閱 Topic 範例 |
|---|---|
| 低（< min_delta） | `{sn}/#` |
| 中（< 2×min_delta） | `{sn}/1/#`, `{sn}/2/#`, … |
| 高（≥ 2×min_delta） | `{sn}/1/1/#`, `{sn}/1/2/#`, … |

---

## 使用說明

### 方法一：直接執行（開發 / 測試用）

#### 啟動監測與調控程式

```bash
sudo python3 adjust.py
```

完整參數（包含 MQTT 帳密檔、TLS、mTLS 與監控端點）請執行 `plc-adjust --help`；systemd 部署則建議使用 `config/adjust.env.example`，避免站點設定散落在命令列。

#### Dry-run 測試（不影響生產線）

```bash
python3 adjust.py --dry_run --interval 5 --machine_sn TEST01
```

### 方法二：systemd 部署（生產環境推薦）

生產環境應部署經審查的 release wheel，不應直接從 `main`、Git URL 或可變動的工作目錄安裝。完整步驟、虛擬環境路徑、BCC 系統套件、systemd unit、MQTT TLS／密碼檔、檔案權限與驗收清單請參考 [Production deployment guide](docs/PRODUCTION_DEPLOYMENT.md)。可從 [config/adjust.env.example](config/adjust.env.example) 與 [config/machines.yaml.example](config/machines.yaml.example) 建立站點設定。

---

## 日誌格式

所有程式輸出為 **JSON 結構化日誌**，可直接串接 `journald`、Loki、或任何 JSON 日誌聚合器：

```json
{"ts": "2025-01-01T12:00:00", "level": "INFO", "event": "Interval measurement", "delta_bytes": 9500, "interval_s": 60, "active_decoders": 3}
{"ts": "2025-01-01T12:00:00", "level": "INFO", "event": "Decoder spawned", "topic": "FAB01/2/3/#", "pid": 12345}
{"ts": "2025-01-01T12:01:00", "level": "INFO", "event": "Decoder terminated", "topic": "FAB01/2/3/#", "pid": 12345}
```

查詢 systemd journal：

```bash
journalctl -u plc-adjust -f -o json
```

---

## 開發說明

本專案使用 `.github/copilot-instructions.md` 作為 **GitHub Copilot SDD（軟體設計文件）**，記錄完整的編碼規範、架構決策與改版任務。Copilot Coding Agent 及 Copilot CLI 可直接讀取該文件並依規範產生符合專案標準的程式碼。

相關開源文件：

| 文件 | 說明 |
|------|------|
| [CHANGELOG.md](CHANGELOG.md) | 版本異動紀錄（Keep a Changelog 格式） |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 貢獻指南、開發環境設定 |
| [SECURITY.md](SECURITY.md) | 安全漏洞回報政策 |
| [LICENSE](../LICENSE) | Apache License 2.0（全 repo 共用） |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | 執行期與開發依賴授權清單 |
| [docs/PRODUCTION_DEPLOYMENT.md](docs/PRODUCTION_DEPLOYMENT.md) | 正式產線部署、驗收與回復指南 |

```bash
# 安裝開發依賴
pip install -r requirements-dev.txt

# 執行測試
python3 -m pytest
```



## 多機台設定與熱重載

`adjust.py` 現在支援可選的 YAML 設定檔，讓單一服務管理多台機台與多個串列埠：

```yaml
machines:
  - machine_sn: FAB01
    serial_port: ttyACM0
    min_delta: 8192
    max_module: 16
    max_unit: 8
  - machine_sn: FAB02
    serial_port: ttyUSB0
    min_delta: 4096
```

啟動時加入 `--config /etc/plc-edgeflow/config.yaml`。更新設定檔後送出 `SIGHUP` 即可熱重載，不必重啟整個監測服務。

## Decoder 處理管線

`decoder.py` 支援兩種處理模式：

- `--processor lineprotocol`：將正規化後的 PLC 點位資料轉為 InfluxDB line protocol，透過 JSON 結構化日誌輸出。
- `--processor sqlite`：將正規化後的 PLC 點位資料寫入 SQLite（WAL 模式）。

所有處理失敗的訊息都會寫入本地 SQLite dead-letter queue，避免 broker 短暫故障或資料格式異常時直接遺失。

## 健康檢查與監控

`adjust.py` 會提供兩個 HTTP 端點：

- `GET /healthz`：回傳當前 machine context 狀態與最近一次 reload 結果
- `GET /metrics`：Prometheus 格式指標，包含 `plc_bytes_delta`、`plc_active_decoders_count`、`plc_decoder_crashes_total`、`plc_ebpf_attach_errors_total`

範例：

```bash
curl http://127.0.0.1:9108/healthz
curl http://127.0.0.1:9108/metrics
```

## 開發測試

安裝完相依套件後可直接執行：

```bash
python3 -m pytest
```
