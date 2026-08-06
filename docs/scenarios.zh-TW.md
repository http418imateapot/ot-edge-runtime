# 工廠情境

English version: [scenarios.md](scenarios.md)

本文說明 `ot-edge-runtime` 所設想的廠區現場情境：出了什麼問題、為什麼常見作法不合用、本專案改用什麼方式，以及每個情境的實際操作範例。若你想看的是逐元件的架構視角，請從 [`architecture.md`](architecture.md) 開始。

## 場景設定

一台工業單板電腦——DIN 導軌上的無風扇機箱，或機台電控箱內的 Jetson / i.MX8 等級模組。它夾在 OT 與 IT 之間：

- **往下**，是 PLC 或一組感測器，通常透過串列埠連接。
- **往上**，是廠區網路：MQTT broker、historian、MES。
- **機器內部**，數個必須互不干擾的常駐程式，跑在會無預警斷電的儲存媒體上。

兩個限制決定了一切：

1. **資源小而固定。** 沒有橫向擴展這回事。一個常駐就吃掉數百 MB 記憶體的控制平面，是這台機器負擔不起的。
2. **沒有人會登入。** 這台機器必須在斷電、斷網、以及某個工作負載失控的情況下，靠自己撐過去。

---

## 情境一 — 高頻量測時採集端塞車

**現場。** 半導體量測製程以高取樣率執行。PLC 從串列埠推送點位資料，SBC 上的消化程式解析後轉發到 MQTT topic 供 historian 收錄。

**症狀。** 在資料突波期間，消化程式跟不上。串列埠緩衝區塞滿、點位遺失，而遺失在 historian 上只呈現為資料空隙——**不會觸發告警**。更麻煩的是它是間歇性的：只在最快的配方下發生，在測試台上重現不了。

**為什麼常見作法不夠。** 輪詢 `/proc` 或應用自己的計數器，看到的是佇列**已經**堆積之後的結果。把消化程式的數量直接開到尖峰負載，會浪費這台機器沒有的記憶體。而固定數量的實例，對配方切換根本無法反應。

**本專案的作法。** [`autoscale/`](../autoscale/) **以 eBPF 在核心層**觀測串列埠入口流量，因此流量訊號在使用者空間佇列堆積之前就取得。調控程式將該訊號與各機台的門檻值比對，據以增減消化程式的實例數量與 MQTT 訂閱策略。

**操作範例。**

```bash
# 描述機台與其門檻值,並讓調控程式指向它。
# unit 從 /etc/plc-edgeflow/adjust.env 讀取站點層級的覆寫設定;
# 其中的 PLC_CONFIG 指定 YAML 檔(也可用命令列的 --config)。
cp autoscale/config/machines.yaml.example /etc/plc-edgeflow/machines.yaml
cp autoscale/config/adjust.env.example     /etc/plc-edgeflow/adjust.env

# 調控程式與消化程式皆以 systemd unit 執行;
# plc-decoder@.service 是 template unit,每個消化程式實例化一次。
systemctl enable --now plc-adjust.service
systemctl status 'plc-decoder@*.service'
```

當觀測到的速率跨越設定門檻，調控程式即啟停 `plc-decoder@N` 實例。細節（含 TLS、最小權限、rollout 與 rollback 的產線導入檢核）見 [`autoscale.md`](autoscale.md) 與 [`../autoscale/docs/PRODUCTION_DEPLOYMENT.md`](../autoscale/docs/PRODUCTION_DEPLOYMENT.md)。

**限制。** eBPF 探針對核心版本敏感，且需要由系統套件管理器提供的 BCC 綁定。它只做觀測，不會對 PLC 端節流。

---

## 情境二 — 某個工作負載餓死了真正重要的那個

**現場。** 同一台機器現在同時跑採集路徑**以及**其他東西：推論程式、廠商的上傳程式、有人在試車期間裝上去的診斷工具。

**症狀。** 網路抖動時上傳程式進入重試風暴，吃光 CPU 與記憶體，於是採集路徑——那個絕對不能卡住的程式——開始錯過它的時限。沒有任何程式崩潰，所以沒有任何告警。

**為什麼常見作法不夠。** 裸 `systemd` 會在程式死掉後重啟它，但它不會在「重要的工作負載」與「不重要的工作負載」之間強制資源上限，沒有映像檔生命週期，也沒有可供維運工具驅動的 API。K3s 或 KubeEdge 三者都給你——同時附帶叢集控制平面、內嵌資料庫與 CNI，裝在一台整份工作只是解碼串列埠資料的機器上。

**本專案的作法。** [`runtime/`](../runtime/) **直接**使用 `runc` 與 cgroups：每個工作負載跑在具明確資源上限的 OCI 容器中，以 user namespace 隔離，並透過帶認證的 REST API 驅動，而非叢集控制平面。沒有排程器、沒有叢集成員管理，因為只有一個節點。

**操作範例。**

```bash
# API 服務本身以 systemd 執行,綁定 loopback 的 8000 埠。
# API_KEY 來自 EnvironmentFile;所有 /api/* 呼叫都需要它。
systemctl enable --now runc-edge-api.service

# 啟動一個工作負載
curl -sS -X POST http://127.0.0.1:8000/api/containers/start \
  -H "X-API-Key: $API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"container_id":"uploader"}'

# 收緊執行中容器的資源上限——底層是 `runc update`,
# 因此不需重啟即生效。memory_limit 單位為 bytes;
# cpu_shares 是相對權重,不是硬性配額。
curl -sS -X PATCH http://127.0.0.1:8000/api/containers/uploader/resources \
  -H "X-API-Key: $API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"cpu_shares":512,"memory_limit":268435456}'
```

完整端點（`/health`、`/api/containers`、`/api/containers/{id}`、start、stop、resources）、`config.json` 中的 OCI spec，以及 user namespace 所需的 subuid/subgid 設定，見 [`runtime.md`](runtime.md)。

**限制。** 僅單一節點。沒有映像檔 registry、沒有 overlay 網路、沒有排程器。root filesystem 需自行準備。

---

## 情境三 — 寫入設定時斷電,開機後設定壞了

**現場。** 廠區斷電——跳電、UPS 沒撐住，或單純有人把電控箱電源拔了。SBC 正好在那一刻寫入它的設定檔，寫到 eMMC 或 SD 卡上。

**症狀。** 機器開回來時，設定檔被截斷或只寫了一半。依格式不同，程式要嘛拒絕啟動，要嘛更糟——它用一份解析到一半的設定啟動了，然後以微妙錯誤的方式運作。

**為什麼常見作法不夠。** 純文字或 INI 檔完全沒有原子性可言，寫到一半就是壞檔。SQLite 能正確解決，但它帶來 WAL、page cache 與一份相依——對一台快閃空間與記憶體都吃緊、只需要存四十組 key/value 的裝置而言太重了。

**本專案的作法。** [`edgeconf/core/`](../edgeconf/core/) 是一個小型 C 語言 key/value 儲存引擎，尺寸就是為這道空隙量的——刻意落在「SQLite 太重」與「純文字檔太脆」之間。它提供函式庫（`librobustcfg`）與 CLI（`robust_cfg_tool`），測試套件中包含專門針對寫入撕裂（torn write）的故障注入案例。

**操作範例。**

```bash
make -C edgeconf/core          # 建置 librobustcfg 與 robust_cfg_tool
make -C edgeconf/core test     # 含 test_fault_inject 與 test_concurrent
```

應用程式可透過內附的 pkg-config 檔連結它，或用 `robust_cfg_tool` 從 shell 操作。見 [`edgeconf-core.md`](edgeconf-core.md)。

**限制。** 沒有 WAL：多個 key 的原子更新（單一交易）需由應用層自行保護。它是 key/value 儲存，不是資料庫。

---

## 情境四 — 新設定必須立刻生效,而且不能重啟任何程式

**現場。** OTA 推送改了一個取樣率或門檻值。機器上有五個程式在意這個數值：採集迴圈、消化程式、上傳程式、看門狗、本地 UI。

**症狀。** 為了一個數字重啟五個程式，代表採集會出現空隙——而且是在生產中。於是改成每個程式各自輪詢那個檔案，既耗 CPU，又仍然有競態：兩個程式同時對同一個檔案做 read-modify-write 會互相覆蓋，而讀取端可能讀到寫到一半的檔案。

**為什麼常見作法不夠。** 輪詢既反應慢又浪費。臨時寫的檔案鎖，往往在五條程式碼路徑中漏掉一條。而寫死綁定 D-Bus 的通知機制，在 OpenWrt 或 uClinux 等級的裝置上根本不存在——那裡有的是 ubus；在極簡 image 上更是兩者皆無。

**本專案的作法。** [`edgeconf/patterns/`](../edgeconf/patterns/) 是一個同步常駐程式，讓設定檔可以安全地被共享：

- **原子且持久的寫入** — 先寫 `.tmp`、`fsync`、`rename()`，再對父目錄 `fsync`。單靠 rename 只能對其他讀者原子，擋不住斷電;那兩個 `fsync` 才是讓機器不會開機看到空設定檔的關鍵。
- **跨程序互斥** — 用 `.lock` 檔搭配 `flock(LOCK_EX)`，把整個 read-modify-write cycle 包起來。
- **Delta 廣播** — `inotify` 偵測變更後，常駐程式比對前後快照，**每個變更的 key 發送一個獨立事件**——**被刪除的 key 也會發出事件**，訂閱端才不會永遠抱著已退役的值。監聽端毫秒級反應，完全不需輪詢。
- **可抽換的傳輸層** — 廣播走的是 `include/ipc_backend.h` 定義的 port。D-Bus 是隨附的參考 adapter；ubus 或 Unix socket 可以在常駐程式毫不知情的情況下加進來。
- **來源可信** — 常駐程式擁有 well-known bus name，訂閱端以它過濾，本機非特權行程無法偽造設定變更。

**操作範例。**

```bash
# 終端機 1 — 常駐程式監看檔案並廣播 delta
./robust_config --ipc-address session --log-stderr watch

# 終端機 2 — 任何想要反應的程式訂閱它
./robust_config --ipc-address session --log-stderr dashboard

# 終端機 3 — OTA 推送,或維運人員,改了一個 key
./robust_config --log-stderr write --key sample_rate --value 120
```

終端機 2 會在毫秒內印出下列內容，且過程中沒有任何程式被重啟：

```
ConfigChanged: key=sample_rate value=120
```

若改為從檔案中刪掉那個 key，每個訂閱端也會被告知，而不是留著一個過期的值：

```
ConfigChanged: key=sample_rate removed (was 120)
```

完整設計、傳輸層 contract,以及如何新增 adapter,見 [`edgeconf-patterns.md`](edgeconf-patterns.md)。

**限制。** 這個常駐程式負責分發與保護檔案，它不解讀那些值的意義。目前只實作了 D-Bus adapter。

---

## 四者如何組合

四個元件各自獨立開發、也都能單獨使用，但它們被設計時所設想的組合方式是：

1. `runtime/` 給每個工作負載一個資源上限，讓情境二不會拖垮情境一。
2. `autoscale/` 依核心層訊號決定該有幾個消化程式，並請求 runtime 執行擴縮。
3. `edgeconf/core/` 保存前兩者所讀取的門檻與設定，放在會斷電的儲存媒體上。
4. `edgeconf/patterns/` 把設定的變更同時分發給每一個程式，不需重啟。

採用其中一個並不需要採用其餘三個。本 repo **沒有**跨元件整合測試——上述組合是設計意圖，不是已驗證的端到端管線。

## 本專案不是什麼

- **不是安全系統。** 本專案未通過 IEC 62443、IEC 61508 或 ISO 13849 認證。它是監控與資料路徑工具組，不是安全儀控、緊急停止或閉迴路機台控制元件。MIT License 不提供保固與賠償，導入驗證仍屬系統整合方的責任。
- **不是編排器。** 單一節點，沒有排程器，沒有叢集。
- **不具 Linux 以外的可攜性**，且部分功能對核心版本敏感。
