# edgeconf/patterns — 容錯設定交換與同步常駐程式

> Component documentation for `edgeconf/patterns/` (originally the standalone `robust-config-exchange` repository). Source lives in [`edgeconf/patterns/`](../edgeconf/patterns/); see the [monorepo README](../README.md) for how the components fit together.

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](../LICENSE)

---

## 簡介

### 問題背景

隨著嵌入式單板電腦 (如 Jetson Nano、Raspberry Pi 等) 成本降低、易於取得，開發人員能快速構建應用程式並部署於邊緣運算場景。然而在上述環境中，多個常駐程序（採集、推論、上傳、看門狗、UI 等）往往需要共享組態參數，並在**不重啟任何程序**的情況下讓 OTA 下發的新設定立刻生效，導致以下問題：

* **競爭鎖與資料一致性**：多程序同時讀寫 config 檔容易發生競態，導致資料損毀或截斷。
* **效能瓶頸**：頻繁輪詢或全量讀取導致系統反應遲緩。
* **部署脆弱**：路徑硬編、依賴圖形 session bus，在 systemd 服務與無頭設備上無法正常運作。

### 解決方案

本元件示範一套可在既有嵌入式開發模式下落地的工業級設計：

* **原子寫入**：write-to-.tmp + `rename()`，讀者永遠看到完整且一致的設定檔。
* **斷電持久性**：`rename()` 之前對暫存檔 `fsync()`、之後對父目錄 `fsync()`。`rename()` 只保證「對其他行程原子」，不保證「對斷電持久」——少了這兩個 `fsync`，rename 可能先落盤而資料還沒，開機就看到空的設定檔。
* **跨程序互斥**：`.lock` 檔 + `flock(LOCK_EX)` 串接多程序的 read-modify-write cycle，防止覆蓋競態。
* **Delta 廣播**：`inotify` 偵測變更後比對前後快照，每個變更 key 發送獨立事件；**被刪除的 key 也會發出事件**，否則訂閱端會永遠保留已退役的值。
* **傳輸中介層**：廣播管道是可抽換的 adapter，而非寫死的 D-Bus 相依（見下節）。
* **來源可信**：常駐程式取得 well-known bus name，訂閱端以 `sender=` 過濾，本機其他行程無法偽造設定變更通知。
* **可觀測性**：以 `syslog(3)` 取代 `printf`，支援 `--log-level` 及 `journald` 整合。
* **Crash Handler**：僅使用 async-signal-safe 的 `write()` 與 `backtrace_symbols_fd()`。
* **Systemd 整合**：提供 `.service` unit 及 watchdog（`sd_notify`）支援，無需依賴 libsystemd。

### 系統需求

* **作業系統**：Linux 核心 2.6.27+（`inotify_init1` 需要）
* **函式庫**：`glibc >= 2.4`；D-Bus adapter 另需 `libdbus-1-dev`

---

## 傳輸中介層（IPC port）

常駐程式需要通知其他程序「某個 key 變了」，但**用什麼機制通知，完全取決於平台**：

* systemd / D-Bus 發行版（Jetson、Raspberry Pi 上的 Debian、Yocto、Buildroot）本來就有 message bus；
* OpenWrt / uClinux 這類裝置有的是 ubus，根本沒有 D-Bus；
* 極簡或容器化 image 可能兩者皆無，只剩下 Unix domain socket。

這些差異不應該滲進常駐程式的邏輯。[`include/ipc_backend.h`](../edgeconf/patterns/include/ipc_backend.h) 定義了 port，每個傳輸方式是它背後的 adapter（strategy pattern），在**編譯期**選定，因此產出的執行檔只連結一個實作、只帶進一組函式庫。

常駐程式只談 key/value delta，看不到 `DBusConnection`、`ubus_context` 或 socket fd。

### Port 的介面

| 函式 | 職責 |
|------|------|
| `open` | 連線到 `address` 指定的端點（`address` 由 adapter 自行解讀，可為 NULL 表示預設值） |
| `close` | 釋放 `open` 取得的資源；可重複呼叫，對未成功開啟的 channel 也安全 |
| `publish` | 廣播一筆 key/value delta，含 `deleted` 旗標以區分「key 被刪除」與「值變成空字串」。**線格式由 adapter 自行決定** |
| `subscribe` | 登記收到 delta 時要呼叫的 callback，不阻塞 |
| `run` | 阻塞式派送迴圈，把收到的 delta 交給 callback |

Adapter 不得寫入 stdout——呈現方式屬於呼叫端的職責；診斷訊息一律走 `logger.h`。

### 目前實作的 adapter

| Backend | 檔案 | 狀態 | 適用平台 |
|---------|------|------|----------|
| `dbus` | [`src/ipc_dbus.c`](../edgeconf/patterns/src/ipc_dbus.c) | **參考實作，預設** | 任何有 D-Bus 的 systemd 發行版 |
| `ubus` | — | 未實作 | OpenWrt / uClinux |
| `unix-socket` | — | 未實作 | 無 bus 的極簡 image |

新增 adapter 的作法：依 `include/ipc_backend.h` 開頭的 contract 撰寫 `src/ipc_<name>.c`，透過 `ipc_transport()` 曝露它，再把 `<name>` 加進 Makefile 的 `IPC_BACKENDS`。**port 以外的程式碼都不需要改動。**

未實作的 backend 會讓建置直接失敗，而不是默默退回預設值：

```bash
make IPC_BACKEND=ubus
# Makefile:41: *** IPC_BACKEND='ubus' is not implemented. Available: dbus. ...
```

### D-Bus adapter 的線格式

該 adapter 把每筆 delta 包成一個字串參數的 D-Bus signal：

```json
{"interface_version":2,"key":"sample_rate","value":"120","deleted":false}
```

`interface_version` 在 payload schema 變更時遞增，讓舊版訂閱者能偵測不相容而非誤讀欄位（v1 沒有 `deleted` 欄位，也無從表達刪除）。key 與 value 皆經 JSON 跳脫，含引號或反斜線的值可以完整來回。此格式是 adapter 的內部細節，port 之上的程式碼看不到它。

訊號以 well-known name `com.example.RobustConfig` 送出——`src/ipc_dbus.c` 在首次 publish 前呼叫 `dbus_bus_request_name()`，訂閱端則以 `sender='com.example.RobustConfig'` 過濾。兩者缺一，本機任何被允許發 signal 的行程都能偽造 `ConfigChanged`，而訂閱端會照單全收。`dbus/com.example.RobustConfig.conf` 這份 system bus ACL 只允許常駐程式的帳號擁有該名稱。

---

## 安裝與編譯

### 1. 安裝必要套件

```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libdbus-1-dev
```

### 2. 取得並編譯

```bash
git clone https://github.com/http418imateapot/ot-edge-runtime.git
cd ot-edge-runtime/edgeconf/patterns
make                      # 等同 make IPC_BACKEND=dbus
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
  watch     Monitor config file; broadcast key/value deltas and removals
  dashboard Receive config deltas and display them
  dump      Print current config to stdout

IPC transport: dbus (compiled in)

Options:
  --config PATH          Config file path
  --ipc-address ADDR     IPC endpoint for the dbus transport
                         (syntax: system | session; default: system)
  --bus ADDR             Deprecated alias for --ipc-address
  --log-level LEVEL      error|warn|info|debug (default: info)
  --log-stderr           Log to stderr instead of syslog
  --dry-run              Print actions without executing them
  --key KEY              Key to write (write mode)
  --value VAL            Value to write (write mode)
  --help                 Show this help and exit
```

`--ipc-address` 的合法值由編譯進去的 adapter 決定；D-Bus adapter 接受 `system` 與 `session`。`--bus` 是抽象層出現前的舊寫法，仍然可用。

---

## 操作範例

### 啟動監控（watch daemon）

```bash
./robust_config --ipc-address session --log-stderr watch
```

### 啟動 Dashboard

```bash
./robust_config --ipc-address session --log-stderr dashboard
```

### 更新設定（觸發 Delta 廣播）

```bash
./robust_config --ipc-address session --log-stderr write --key sample_rate --value 120
```

### 查看目前設定

```bash
./robust_config dump
```

### 範例 Dashboard 輸出

```
ConfigChanged: key=sample_rate value=120
ConfigChanged: key=threshold value=0.90
ConfigChanged: key=legacy_mode removed (was on)
```

輸出是傳輸中立的 key/value——不論底下是哪個 adapter，呈現格式都一致。

---

## 測試

### 單元測試（不需任何 bus）

```bash
make test
```

### 整合測試（需 D-Bus session）

```bash
dbus-run-session -- bash tests/test_integration.sh ./robust_config
```

兩者都在 monorepo 根目錄的 [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) 中執行。

---

## 專案結構

```
include/
  ipc_backend.h     傳輸中介層：port 定義與 adapter contract
src/
  robust_config.c   主程式：CLI 解析、模式分派
  ipc_backend.c     port 的通用包裝（選定 adapter、參數檢查、錯誤字串）
  ipc_dbus.c        D-Bus adapter（versioned JSON payload）— 參考實作
  logger.h/.c       syslog 封裝，支援 --log-level
  crash_handler.h/.c  async-signal-safe crash handler
  config_io.h/.c    設定檔讀寫（原子寫入、互斥鎖、diff）
  watchdog.h/.c     sd_notify watchdog（不依賴 libsystemd）
dbus/
  com.example.RobustConfig.conf   D-Bus system bus ACL
systemd/
  robust-config-watch.service     systemd service unit
tests/
  test_write_read.sh    單元測試（write/dump/dry-run/concurrent）
  test_integration.sh   端到端整合測試
```

`.github/workflows/` 下另有原始 repo 的 CI 設定。它保留在此僅為存續來源專案的內容；GitHub 只執行 repo 根目錄的 workflow，該檔案在 monorepo 中不會被執行。
