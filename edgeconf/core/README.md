# Linux-based Robust-Binary-Config

進階版的 [robust-config-exchange](https://github.com/http418imateapot/robust-config-exchange) 專案，目的是為輕量、使用檔案來保存系統設定的嵌入式系統 App，設計結構化二進位檔案格式，與分段鎖 (byte-range locking) 讀寫方式，提供更高的並行讀寫效率，也避免 flock 整個檔案鎖死造成的效能瓶頸。


---

## 簡介

隨著單板電腦成本降低，開發小型工具程式快速簡便，適合用來快速建置邊緣運算工具、自動化控制周邊工具。但是這類小工具程式為了簡化與盡快上線，可能會使用 log、config 檔案當作程式交換訊息，或是儲存系統設定的媒介，也帶來了檔案讀寫衝突，資料損毀或不一致的風險。

在 [robust-config-exchange](https://github.com/http418imateapot/robust-config-exchange) 專案中，使用了 flock 的方式避免上述問題，但是 flock 整份檔案鎖死的方式，會降低應用程式並行讀寫的效率。因此這個專案示範了二進制 config 檔案結構設計，與可以分區讀寫、使用分段鎖的開發放式。

## 系統需求

* 作業系統：Linux 核心版本至少為 2.6.13，建議使用：Ubuntu 18.04 / 20.04 / 22.04；Raspbian (基於 Debian)
* 軟體需求：GNU C Library (glibc) 版本至少為 2.4

---

## 安裝與編譯

### 1. 安裝必要套件

```bash
sudo apt-get update
sudo apt-get install build-essential
```

### 2. 下載專案程式碼

```bash
git https://github.com/http418imateapot/robust-binary-config
cd robust-binary-config
```

### 3. 編譯專案

```bash
make
```

成功編譯後會生成執行檔 robust_bin_config。


### 4. 清理專案

```bash
make clean
```

---

## 範例程式 ``robust_bin_config`` Usage

```shell
Usage: ./robust_bin_config <read|write> <index> [<key> <value>]
  read  : Read a record at the specified index
  write : Write a record at the specified index with the given data
          Requires: <key> <value>
```

---

## 操作範例

### 1. 寫入設定資料

範例：寫入一筆記錄到第 0 筆索引位置

```bash
./robust_bin_config write 0 sensor1 temperature=30
```

### 2. 讀取設定資料

範例：讀取第 0 筆索引位置的記錄

bash
```
./robust_bin_config read 0
```

### 3. 範例輸出

寫入資料成功

```plaintext
Write success!
```

讀取資料成功

```plaintext
Read success! key=sensor1, value=temperature=30
```

當檔案不存在時的讀取錯誤

```plaintext
File 'config.bin' does not exist. Please write to the file first.
Read failed!
```

