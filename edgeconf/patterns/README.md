# Linux-based Robust-Config-Exchange

---

## 簡介

### 問題背景

隨著嵌入式單板電腦 (如 Jetson Nano、Raspberry Pi 等) 成本降低、易於取得，開發人員能快速構建應用程式並部署於邊緣運算場景。然而在上述環境中，多個常駐程序（採集、推論、上傳、看門狗、UI 等）往往需要共享組態參數，並在**不重啟任何程序**的情況下讓 OTA 下發的新設定立刻生效，導致以下問題：

* **競爭鎖與資料一致性**：多程序同時讀寫 config 檔容易發生競態，導致資料損毀或截斷。
* **效能瓶頸**：頻繁輪詢或全量讀取導致系統反應遲緩。
* **部署脆弱**：路徑硬編、依賴圖形 session bus，在 systemd 服務與無頭設備上無法正常運作。

### 解決方案

本專案示範一套可在既有嵌入式開發模式下落地的工業級設計：

* **原子寫入**：write-to-.tmp + `rename()`，讀者永遠看到完整且一致的設定檔。
* **跨程序互斥**：`.lock` 檔 + `flock(LOCK_EX)` 串接多程序的 read-modify-write cycle，防止覆蓋競態。
* **Delta 廣播**：`inotify` 偵測變更後，比對前後快照，每個變更 key 發送獨立 D-Bus signal，payload 含 `interface_version` 版本欄位，為未來相容性預留空間。
* **System Bus 優先**：預設使用 D-Bus system bus，適用無頭設備與 systemd 管理服務。
* **可觀測性**：以 `syslog(3)` 取代 `printf`，支援 `--log-level` 及 `journald` 整合。
* **Crash Handler**：僅使用 async-signal-safe 的 `write()` 與 `backtrace_symbols_fd()`。
* **Systemd 整合**：提供 `.service` unit 及 watchdog（`sd_notify`）支援，無需依賴 libsystemd。

### 系統需求

* **作業系統**：Linux 核心 2.6.27+（`inotify_init1` 需要）；建議 Ubuntu 18.04/20.04/22.04 或 Raspbian
* **函式庫**：`glibc >= 2.4`、`libdbus-1-dev`

---

## 安裝與編譯

### 1. 安裝必要套件

```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libdbus-1-dev
```

### 2. 下載並編譯

```bash
git clone https://github.com/http418imateapot/robust-config-exchange.git
cd robust-config-exchange
make
```

### 3. 安裝（系統部署）

```bash
sudo make install            # 安裝至 /usr/local/bin，並部署 D-Bus policy 與 systemd service
sudo systemctl daemon-reload
sudo systemctl enable --now robust-config-watch.service
```

### 4. 跨平台交叉編譯（aarch64）

```bash
make CC=aarch64-linux-gnu-gcc
```

### 5. 清理

```bash
make clean
```

---

## 設定檔格式

```
# robust-config key=value store
sample_rate=100
threshold=0.85
upload_url=https://example.com/upload
model_path=/opt/models/v2.bin
```

設定檔路徑解析順序（優先序由高至低）：

1. `--config PATH` CLI 參數
2. `$ROBUST_CONFIG_PATH` 環境變數
3. `/etc/robust-config/config.conf`（編譯預設值）

---

## Usage

```
Usage: robust_config [options] <mode>

Modes:
  write     Update one config key (requires --key and --value)
  watch     Monitor config file and broadcast D-Bus delta signals
  dashboard Receive D-Bus config signals and display them
  dump      Print current config to stdout

Options:
  --config PATH          Config file path
  --bus session|system   D-Bus bus type (default: system)
  --log-level LEVEL      error|warn|info|debug (default: info)
  --log-stderr           Log to stderr instead of syslog
  --dry-run              Print actions without executing them
  --key KEY              Key to write (write mode)
  --value VAL            Value to write (write mode)
  --help                 Show this help and exit
```

---

## 操作範例

### 啟動監控（watch daemon）

```bash
./robust_config --bus session --log-stderr watch
```

### 啟動 Dashboard

```bash
./robust_config --bus session --log-stderr dashboard
```

### 更新設定（觸發 Delta 廣播）

```bash
./robust_config --bus session --log-stderr write --key sample_rate --value 120
```

### 查看目前設定

```bash
./robust_config dump
```

### 範例 Dashboard 輸出

```
ConfigChanged: {"interface_version":1,"key":"sample_rate","value":"120"}
ConfigChanged: {"interface_version":1,"key":"threshold","value":"0.90"}
```

---

## 測試

### 單元測試（不需 D-Bus）

```bash
make test
```

### 整合測試（需 D-Bus session）

```bash
bash tests/test_integration.sh ./robust_config
```

---

## 專案結構

```
src/
  robust_config.c   主程式：CLI 解析、模式分派
  logger.h/.c       syslog 封裝，支援 --log-level
  crash_handler.h/.c  async-signal-safe crash handler
  config_io.h/.c    設定檔讀寫（原子寫入、互斥鎖、diff）
  dbus_ipc.h/.c     D-Bus 發送與接收（versioned JSON payload）
  watchdog.h/.c     sd_notify watchdog（不依賴 libsystemd）
dbus/
  com.example.RobustConfig.conf   D-Bus system bus ACL
systemd/
  robust-config-watch.service     systemd service unit
tests/
  test_write_read.sh    單元測試（write/dump/dry-run/concurrent）
  test_integration.sh   端到端整合測試
.github/workflows/
  ci.yml              CI：build + cppcheck + unit test + aarch64 cross-compile
```

