# 用 Linux 原生容器打造 OT 系統資源安全限制與 REST API 管理工具包

## 簡介

使用 Linux 原生容器機制 (cgroups)，示範 OT 程式系統資源安全性限制，與開發輕量的容器管理 REST API，為嵌入式 OT2IT 單板電腦產品，提供低系統資源需求的輕量容器化方案。

## 專案架構

```
python-runc-container/
├── container_rootfs/   # Guest OS RootFS (可用 Buildroot/Yocto 生成)
├── config.json         # runc 容器配置文件
├── app/sample.sh       # OT2IT 範例程式
├── api.py              # 容器管理 REST API 程式
├── requirements.txt    # 容器管理 REST API 程式相依套件
└── README.md           # 專案說明文件
```

## Linux 版本需求

* Kernel：建議 4.19 以上
* 核心功能：必須啟用 cgroups，與主要 namespaces(PID、NET、IPC、UTS、USER、MOUNT)
* 適用平台：嵌入式工控設備，如 Raspberry Pi
* 其他：python3, python3-pip


## 安裝步驟

### 1. 下載專案

```bash
git clone https://github.com/http418imateapot/restful-runc-container.git
cd restful-runc-container
```

### (非必要) 2. 建立 Guest OS RootFS

如果你尚未準備好 RootFS，請保持 ``container_rootfs`` 目錄底下沒有檔案即可。

或者你可以使用 Buildroot、Yocto 裁剪出最小化 Linux image，將生成的 RootFS 解壓縮到 ``container_rootfs`` 目錄中。

### 3. 安裝原生容器工具

```bash
sudo apt-get install runc cgroup-tools
```

### 4. 安裝容器管理 REST API 程式 (Python)

```bash
sudo apt-get install python3-full
sudo apt-get install python3-pip
python3 -m venv venv
source venv/bin/activate
pip install --break-system-packages -r requirements.txt
uvicorn api:app
```

### 5. 更新與安裝 OT2IT 程式

非必要，請依傳案實際需求調整；預設使用 ``app`` 目錄。

### 6. 配置原生容器文件 -- ``config.json``

可根據需求調整 resources 欄位，以動態控制 CPU、記憶體等資源分配。本範例的重點有：

* 容器可用的最大記憶體限制
* 容器的 CPU 權重
* 容器隔離的 Linux namespace (PID, 掛載點隔離)


### 7. 啟動容器

使用 runc 以 config.json 啟動容器：

```bash
sudo runc run OT2IT-Sample
```

### 8. 容器管理 API 使用說明

提供 RESTful API，提供控制容器的啟停與資源調整。

* POST /api/containers/start
* POST /api/containers/stop
* PATCH /api/containers/{container_id}/resources
* GET /api/containers
* GET /api/containers/{container_id}

可於安裝並啟動 REST API 後，以瀏覽器打開 API 的線上文件與測試工具 (Swagger)：

```
http://127.0.0.1:8000/docs
```

