# Linux-based Robust-Config-Exchange

---

## 簡介

### 問題背景

隨著嵌入式單板電腦 (如 Jetson Nano、Raspberry Pi 等) 成本降低、易於取得，且開發環境與編譯工具日益完善，開發人員能快速構建應用程式與小工具，並將其部署於邊緣運算場景中。然而在上述應用程式中，經常使用 config 或 log 檔案進行簡單的資訊交換，甚至直接讀寫檔案作為 IPC (進程間通訊) 的方法，造成以下問題：

* 競爭鎖與資料一致性問題：同時操作 log 檔案容易發生檔案鎖衝突，導致資料損毀或不一致。
* 效能瓶頸：頻繁的檔案 I/O 操作造成系統反應遲緩與效能下降。

### 解決方案

本範例提出可在既有開發模式與人力限制下，改善系統穩定性與可維護性的方法：

* 安全檔案操作：透過 flock 檔案鎖管理檔案操作。
* 即時事件處理：使用 DBus 替代頻繁的檔案 I/O。
* 錯誤追蹤強化：設計 Crash handler，自動捕捉 SIGSEGV、SIGABRT 等錯誤訊號。

### 系統需求

* 作業系統
    * Linux 核心版本：至少為 2.6.13
    * 建議使用：Ubuntu 18.04 / 20.04 / 22.04；Raspbian (基於 Debian)
* 軟體需求
    * GNU C Library (glibc) 版本至少為 2.4
    * 系統函式庫與工具：libdbus-1-dev：支援 D-Bus IPC 通訊。
      
### 系統架構概述

* 檔案管理與資訊交換：支援安全寫入共享鎖讀取 (flock) ，防止操作衝突與資料不一致。
* D-Bus 即時通知：透過 DBus 傳送訊號，降低檔案 I/O 負擔。
* Crash Handler：系統崩潰時自動捕捉錯誤訊號，並輸出追蹤資訊。


---

## 安裝與編譯

### 1. 安裝必要套件

```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libdbus-1-dev
```

### 2. 下載專案程式碼

```bash
git clone https://github.com/http418imateapot/robust-config-exchange.git
cd robust-config-exchange
```

### 3. 編譯專案

```bash
make
```

成功編譯後會生成執行檔 robust_config。


### 4. 清理專案

bash
```
make clean
```

---

## 範例程式 "``robust_config``" Usage

```shell
Usage: ./robust_config <mode>
  mode: write      - Write a log entry
        watch      - Watch log file and send DBus signals on changes
        dashboard  - Receive DBus signals and print log messages
```

---

## 操作範例

### 1. 執行監測 Log 程式

監控 Log 檔案:
```bash
./robust_config watch
```

監控 Log D-Bus:
```bash
./robust_config dashboard
```


### 2. 執行模擬寫入 Log 程式

```bash
./robust_config write
```

### 3. 範例輸出

監控 Log 檔案:
```plaintext
Monitoring logs/log.txt for changes...
Sent DBus signal with log: Log entry at Sun Feb  9 09:51:41 2025
Log entry at Sun Feb  9 09:52:05 2025
```

監控 Log D-Bus:
```plaintext
Listening for D-Bus signals...
Received message: Log entry at Sun Feb  9 09:51:41 2025
Log entry at Sun Feb  9 09:52:05 2025
```

