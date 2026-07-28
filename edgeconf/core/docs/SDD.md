# Software Design Document — robust-binary-config

> **版本**: 1.0.0  
> **日期**: 2026-06-30  
> **場景定位**: 適用 Linux 邊緣裝置的輕量嵌入式 Key-Value 設定引擎，填補「SQLite 太重、純文字檔太脆」之間的空隙。

---

## 1. 背景與目標

### 1.1 應用場景

典型部署環境：工廠自動化閘道器、AIoT 感測器集線器、邊緣運算節點（Raspberry Pi、i.MX8、Jetson Nano）。

常見運作型態：
- 多個常駐程序（採集 daemon、上報 daemon、規則引擎）共享同一份設定檔
- 設備可能隨時斷電，設定需嚴格持久化
- 設定格式需隨韌體版本迭代演進
- 設定讀取頻繁（每秒數次），寫入罕見（每分鐘數次或更少）

### 1.2 設計目標

| 目標 | 具體要求 |
|------|---------|
| **可靠性** | 斷電後資料不丟失；CRC32 偵測 bit-flip / flash 損毀 |
| **並發安全** | 多程序並發讀取不阻塞；寫入串行化，不造成資料競爭 |
| **可演進性** | 檔案格式帶 magic / version，支援未來格式遷移 |
| **可整合性** | 以 C library（`.a` / `.so`）形式提供，可被其他程序 link |
| **輕量** | 零外部依賴；單一 C 檔案 + 公開 header |
| **可測試** | 附帶單元測試、並發測試、故障注入測試 |

---

## 2. 系統架構

```
┌─────────────────────────────────────────────────────┐
│                   應用程式 / Daemon                  │
│  robust_cfg_open() / read() / write() / close()     │
└───────────────────────┬─────────────────────────────┘
                        │  librobustcfg.a / .so
┌───────────────────────▼─────────────────────────────┐
│             robust_cfg.c  (Library Core)             │
│                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │
│  │  CRC32 計算  │  │  fcntl 鎖管理 │  │  I/O 層  │  │
│  │  (內建實作)  │  │  (讀者/寫者)  │  │ (read/   │  │
│  └──────────────┘  └──────────────┘  │  write/  │  │
│                                       │  fsync)  │  │
│  ┌─────────────────────────────────┐  └───────────┘  │
│  │  Handle (fd + pthread_mutex_t)  │                 │
│  └─────────────────────────────────┘                 │
└───────────────────────┬─────────────────────────────┘
                        │  POSIX file I/O
┌───────────────────────▼─────────────────────────────┐
│                  config.bin  (二進位格式)             │
│  [File Header 64B][Slot0 300B][Slot1 300B]...        │
└─────────────────────────────────────────────────────┘
```

### 2.1 元件說明

- **`include/robust_cfg.h`** — 公開 API，唯一對外介面
- **`src/robust_cfg_format.h`** — 內部 binary 格式定義（僅 library 內部使用）
- **`src/robust_cfg.c`** — Library 實作：CRC32、locking、I/O、KV 操作
- **`src/cli.c`** — CLI 工具，呼叫 library API，供腳本 / 手動操作使用

---

## 3. 二進位檔案格式 (Format Version 1)

### 3.1 整體佈局

```
Offset  Size   Description
------  ----   -----------
0       64     File Header
64      300    Record Slot 0
364     300    Record Slot 1
...
64 + N*300     Record Slot N-1   (N = capacity)
```

### 3.2 File Header（64 bytes）

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];      // "RCFG" (0x52 0x43 0x46 0x47)
    uint16_t version;       // 格式版本，目前為 1
    uint16_t flags;         // 保留，必須為 0
    uint32_t capacity;      // 最大 slot 數
    uint32_t record_count;  // 目前 VALID 記錄數（快取，可自 slots 重建）
    uint8_t  reserved[44];  // 保留，全 0
    uint32_t header_crc;    // CRC32（涵蓋前 60 bytes）
} robust_cfg_file_header_t; // 共 64 bytes
```

**Magic Number** `"RCFG"` 可以快速驗證檔案身份，防止誤用其他格式檔案。  
**Version** 允許未來格式迭代。Library 會拒絕 version 不符的檔案。  
**header_crc** 保護 header 本身不被損毀（CRC 涵蓋前 60 bytes，排除自身）。

### 3.3 Record Slot（300 bytes）

```c
typedef struct __attribute__((packed)) {
    uint8_t  state;                       // SLOT_EMPTY=0, SLOT_VALID=1,
                                          // SLOT_DELETED=2, SLOT_CORRUPT=0xFF
    uint8_t  reserved[3];                 // 保留，全 0
    uint32_t timestamp;                   // Unix 時間戳（秒）
    char     key[32];                     // Null-terminated，最長 31 字元
    char     value[256];                  // Null-terminated，最長 255 字元
    uint32_t record_crc;                  // CRC32（涵蓋前 296 bytes）
} robust_cfg_record_t; // 共 300 bytes
```

**Slot State** 狀態機：

```
  EMPTY ──write──► VALID ──delete──► DELETED
    ▲                 │                  │
    │                 │ CRC mismatch     │ CRC mismatch
    │                 ▼                  ▼
  repair          CORRUPT ◄──────────────┘
    └──────────────────┘
```

### 3.4 Log-Structured 寫入策略

更新一個 key 的流程：
1. 找到第一個 `EMPTY` slot（target）
2. 找到既有的 `VALID` slot（old，若存在）
3. 寫入 target slot（包含 CRC）
4. `fdatasync` 確保落盤
5. Write-verify：讀回並比對 CRC
6. 將 old slot state 改為 `DELETED`（再次 `fdatasync`）
7. 更新 header `record_count`

此策略確保：即使在步驟 3-5 之間斷電，舊值仍然完好可讀（最多退回上一版）。

---

## 4. 並發模型

### 4.1 Readers-Writer Locking（跨 process）

使用 `fcntl(F_SETLK)` byte-range locking，以整個檔案範圍（`l_start=0, l_len=0`）模擬 POSIX readers-writer lock：

| 操作 | Lock 類型 | 效果 |
|------|----------|------|
| `read` | `F_RDLCK` (entire file) | 多個 readers 可同時持有 |
| `write / delete / compact / repair` | `F_WRLCK` (entire file) | 排他鎖，阻塞所有其他存取 |

讀取時：所有 readers 可並行，寫入時：單一 writer 排他。此設計對讀多寫少的 config 場景是最佳解。

### 4.2 Lock 逾時（Timeout）

使用非阻塞 `F_SETLK` + retry loop，避免原始碼中 `F_SETLKW` 無限阻塞問題：

```
每隔 LOCK_RETRY_SLEEP_MS (10ms) 重試一次
最多重試 LOCK_RETRY_COUNT (500) 次
總逾時：約 5 秒
超時回傳 ROBUST_CFG_TIMEOUT
```

### 4.3 Process-Internal 互斥（同 process 多執行緒）

Handle 內含 `pthread_mutex_t`，保護同一 process 內多執行緒不交錯操作。

---

## 5. 錯誤碼

| 代碼 | 數值 | 說明 |
|------|------|------|
| `ROBUST_CFG_OK` | 0 | 成功 |
| `ROBUST_CFG_ERR` | -1 | 一般 I/O 或系統錯誤 |
| `ROBUST_CFG_NOT_FOUND` | -2 | Key 不存在 |
| `ROBUST_CFG_CORRUPT` | -3 | CRC 不符 / 資料損毀 |
| `ROBUST_CFG_FULL` | -4 | 無可用 EMPTY slot |
| `ROBUST_CFG_TIMEOUT` | -5 | 鎖超時 |
| `ROBUST_CFG_BAD_ARG` | -6 | 非法參數 |

---

## 6. API 規格

詳見 `include/robust_cfg.h`，以下是摘要：

```c
// 開啟（或建立）設定檔
robust_cfg_handle_t *robust_cfg_open(const char *path, uint32_t capacity);

// 關閉 handle、釋放資源
void robust_cfg_close(robust_cfg_handle_t *h);

// 寫入 / 更新 key-value
int robust_cfg_write(robust_cfg_handle_t *h, const char *key, const char *value);

// 讀取 key 的 value
int robust_cfg_read(robust_cfg_handle_t *h, const char *key, char *value, size_t buflen);

// 刪除 key
int robust_cfg_delete(robust_cfg_handle_t *h, const char *key);

// 維護：重置 CORRUPT slots 為 EMPTY，讓空間可被重用
int robust_cfg_repair(robust_cfg_handle_t *h);

// 維護：移除 DELETED / CORRUPT slots，重建緊湊檔案
int robust_cfg_compact(robust_cfg_handle_t *h);
```

---

## 7. 建置系統

### 7.1 CMake 目標

| Target | 說明 |
|--------|------|
| `robustcfg` | 靜態或動態 library（由 `BUILD_SHARED_LIBS` 控制） |
| `robust_cfg_tool` | CLI 工具 |
| `test_read_write` | 基本讀寫 / 邊界測試 |
| `test_concurrent` | 多 process 並發讀寫測試 |
| `test_fault_inject` | 故障注入（模擬斷電）測試 |

### 7.2 快速建置

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### 7.3 Sanitizer 建置

```bash
cmake -B build-asan -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

### 7.4 Cross-compile for aarch64

```bash
cmake -B build-arm \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DBUILD_TESTING=OFF
cmake --build build-arm --parallel
```

---

## 8. 版本策略

遵循 Semantic Versioning：

| 變更類型 | 版本更新 |
|---------|---------|
| 格式 version 升級（新 format）| MAJOR |
| 新增 API 函數 | MINOR |
| Bug fix | PATCH |

格式版本（`hdr.version`）與 library 版本（`ROBUST_CFG_VERSION_MAJOR`）相互對應。

---

## 9. 已知限制與未來方向

| 限制 | 說明 | 未來方向 |
|------|------|---------|
| Advisory lock | 不遵守協議的程序可跳過鎖直接寫檔 | 可配合 file permission（0644 → 0640）與 group 管理 |
| 固定 capacity | 建立後無法動態擴容 | 支援 `robust_cfg_resize()` |
| 無 WAL | 多個 key 的原子更新需應用層保護 | 加入 Write-Ahead Log |
| 線性掃描 | 每次 read/write 都掃整個 slot 陣列（O(N)）| 加入 in-memory hash 索引（open 時建立） |
