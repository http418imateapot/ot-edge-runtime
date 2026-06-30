# robust-binary-config — Copilot Instructions

## 專案定位
輕量嵌入式 Key-Value 設定引擎（C99/C11），適用 Linux 邊緣裝置（Raspberry Pi、i.MX8 等），填補「SQLite 太重、純文字檔太脆」之間的空隙。詳細設計見 `docs/SDD.md`。

## 目錄結構
```
include/robust_cfg.h       ← 唯一對外公開 API
src/robust_cfg_format.h    ← 內部 binary 格式 (magic/header/record/CRC)，僅 library 使用
src/robust_cfg.c           ← library 核心：CRC32、fcntl byte-range locking、I/O
src/cli.c                  ← CLI 工具，只呼叫 library API
tests/                     ← 測試（test_helpers.h + 三個測試執行檔）
CMakeLists.txt             ← 建置系統（CMake 3.12+）
docs/SDD.md                ← 完整軟體設計文件
```

## Binary 格式規格
- **File Header**（64 bytes）：magic `"RCFG"` + version + capacity + record_count + CRC32
- **Record Slot**（300 bytes）：state + timestamp + key[32] + value[256] + CRC32
- Slot state：`EMPTY=0, VALID=1, DELETED=2, CORRUPT=0xFF`
- 寫入採 log-structured（新 slot → fsync → write-verify → 舊 slot 標 DELETED）

## 並發模型
- `F_RDLCK`（entire file）for reads：允許多 process 並行讀取
- `F_WRLCK`（entire file）for writes：排他鎖
- 使用非阻塞 `F_SETLK` + retry（最多 500 次 × 10ms），避免無限阻塞
- Handle 內含 `pthread_mutex_t` 保護同 process 多執行緒

## 錯誤碼
`ROBUST_CFG_OK=0, ERR=-1, NOT_FOUND=-2, CORRUPT=-3, FULL=-4, TIMEOUT=-5, BAD_ARG=-6`

## 建置與測試
```bash
cmake -B build -DBUILD_TESTING=ON && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 程式碼慣例
- C11；`__attribute__((packed))` 用於格式 struct
- 所有對外 API 前綴 `robust_cfg_`；內部 helper 為 static
- 錯誤時回傳負數錯誤碼；成功回傳 `ROBUST_CFG_OK`（0）
- 所有寫入後均呼叫 `fdatasync`；寫入後執行 write-verify（讀回比對 CRC）
- 不使用動態外部依賴；CRC32 於 `robust_cfg.c` 內自行實作
