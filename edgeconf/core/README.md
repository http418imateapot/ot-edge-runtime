# Linux-based Robust-Binary-Config

[![CI](https://github.com/http418imateapot/robust-binary-config/actions/workflows/ci.yml/badge.svg)](https://github.com/http418imateapot/robust-binary-config/actions/workflows/ci.yml)
[![Static Analysis](https://github.com/http418imateapot/robust-binary-config/actions/workflows/static-analysis.yml/badge.svg)](https://github.com/http418imateapot/robust-binary-config/actions/workflows/static-analysis.yml)
[![Python Syntax](https://github.com/http418imateapot/robust-binary-config/actions/workflows/python-syntax.yml/badge.svg)](https://github.com/http418imateapot/robust-binary-config/actions/workflows/python-syntax.yml)
[![SQL Syntax](https://github.com/http418imateapot/robust-binary-config/actions/workflows/sql-syntax.yml/badge.svg)](https://github.com/http418imateapot/robust-binary-config/actions/workflows/sql-syntax.yml)
[![Release](https://img.shields.io/github/v/release/http418imateapot/robust-binary-config)](https://github.com/http418imateapot/robust-binary-config/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**適用 Linux 邊緣裝置的輕量嵌入式 Key-Value 設定引擎**

進階版的 [robust-config-exchange](https://github.com/http418imateapot/robust-config-exchange) 專案。填補「SQLite 太重、純文字檔太脆」之間的空隙，專為工廠自動化閘道器、AIoT 感測器集線器、邊緣運算節點等場景設計。

詳細架構與格式規格見 **[docs/SDD.md](docs/SDD.md)**。

---

## 特性

| 特性 | 說明 |
|------|------|
| **CRC32 完整性保護** | File Header 與每筆 Record 均有獨立 CRC32，可偵測 bit-flip 與 flash 損毀 |
| **斷電可靠寫入** | Log-structured 寫入 + `fdatasync` + write-verify，斷電最多退回上一版有效值 |
| **並發讀取** | `fcntl` byte-range `F_RDLCK`：多程序可同時讀取，互不阻塞 |
| **排他寫入** | `F_WRLCK` 確保寫入串行化，不造成資料競爭 |
| **Lock Timeout** | 非阻塞 `F_SETLK` + retry（500 × 10ms），避免 crash 後無限鎖死 |
| **執行緒安全** | Handle 內含 `pthread_mutex_t`，保護同程序多執行緒 |
| **格式可演進** | Header 帶 magic `"RCFG"` + version，支援未來格式遷移 |
| **零外部依賴** | 純 POSIX C11，CRC32 內建實作，無需任何第三方函式庫 |

---

## 系統需求

- **OS**: Linux 核心 ≥ 2.6.13（建議 Ubuntu 18.04+、Raspbian/Debian）
- **Compiler**: GCC 5+ 或 Clang 3.6+（需支援 C11 / `_Static_assert`）
- **CMake**: ≥ 3.12
- **glibc**: ≥ 2.4

---

## 安裝與編譯

### 1. 安裝必要套件

```bash
sudo apt-get update
sudo apt-get install build-essential cmake
```

### 2. 下載專案程式碼

```bash
git clone https://github.com/http418imateapot/robust-binary-config
cd robust-binary-config
```

### 3. 編譯

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
```

或使用 Makefile 包裝：

```bash
make
```

成功後產生：
- `build/librobustcfg.a` — 靜態函式庫
- `build/robust_cfg_tool` — CLI 工具

### 4. 執行測試

```bash
ctest --test-dir build --output-on-failure
# 或
make test
```

### 5. 清理

```bash
make clean
```

---

## CLI 工具 `robust_cfg_tool` 使用說明

```
Usage: robust_cfg_tool <file> <command> [args...]

Commands:
  read   <key>           Read value for the given key
  write  <key> <value>   Write (or update) a key-value pair
  delete <key>           Delete a key
  repair                 Reset CORRUPT slots to EMPTY
  compact                Remove DELETED/CORRUPT slots
```

---

## 操作範例

### 1. 寫入設定資料

```bash
./build/robust_cfg_tool config.bin write server_url https://example.com
./build/robust_cfg_tool config.bin write sample_rate 100
./build/robust_cfg_tool config.bin write threshold 0.85
```

### 2. 讀取設定資料

```bash
./build/robust_cfg_tool config.bin read server_url
```

輸出：
```
key=server_url value=https://example.com
```

### 3. 更新已有 Key

```bash
./build/robust_cfg_tool config.bin write sample_rate 200
./build/robust_cfg_tool config.bin read  sample_rate
# key=sample_rate value=200
```

### 4. 刪除 Key

```bash
./build/robust_cfg_tool config.bin delete threshold
```

### 5. 維護操作

```bash
# 修復損毀 slots（開機後建議執行）
./build/robust_cfg_tool config.bin repair

# 回收已刪除 slots 的空間
./build/robust_cfg_tool config.bin compact
```

---

## 在 C 程式中使用

```c
#include "robust_cfg.h"

// 開啟（或建立）設定檔，預設容量 64 筆
robust_cfg_handle_t *h = robust_cfg_open("/etc/myapp/config.bin", 0);
if (!h) { perror("open"); exit(1); }

// 寫入
robust_cfg_write(h, "server_url", "https://example.com");

// 讀取
char url[ROBUST_CFG_VALUE_MAX];
if (robust_cfg_read(h, "server_url", url, sizeof(url)) == ROBUST_CFG_OK)
    printf("url = %s\n", url);

// 刪除
robust_cfg_delete(h, "server_url");

// 維護
robust_cfg_repair(h);   // 修復損毀 slots
robust_cfg_compact(h);  // 回收空間

robust_cfg_close(h);
```

與 CMake 專案整合：

```cmake
find_library(ROBUSTCFG_LIB robustcfg REQUIRED)
target_link_libraries(my_daemon PRIVATE ${ROBUSTCFG_LIB} pthread)
target_include_directories(my_daemon PRIVATE /usr/local/include)
```

---

## 檔案格式概覽

```
Offset    Size    Description
────────  ──────  ───────────────────────────────────────────────
0         64      File Header: magic "RCFG" + version + capacity
                              + record_count + CRC32
64        300     Record Slot 0: state + timestamp + key[32]
                               + value[256] + CRC32
364       300     Record Slot 1
...
```

詳細規格見 [docs/SDD.md § 3](docs/SDD.md)。

---

## 進階建置選項

### Sanitizer 建置（開發 / CI 用）

```bash
cmake -B build-asan \
  -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

### aarch64 交叉編譯

```bash
sudo apt-get install gcc-aarch64-linux-gnu
cmake -B build-arm \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DBUILD_TESTING=OFF
cmake --build build-arm --parallel
```

### 動態函式庫

```bash
cmake -B build -DBUILD_SHARED_LIBS=ON
cmake --build build --parallel
```

---

## 已知限制

| 限制 | 說明 |
|------|------|
| Advisory lock | 不遵守協議的程序可繞過鎖直接寫檔 |
| 固定 capacity | 建立後無法動態擴容 |
| 線性掃描 | read/write 均為 O(N)，N 為 capacity |
| 無 WAL | 多個 key 的原子更新需應用層自行保護 |

---

## License

MIT — 詳見 [LICENSE](LICENSE)

