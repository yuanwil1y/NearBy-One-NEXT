# Agent D — Waveshare ESP32-C6-Touch-LCD-1.9 / ESP-IDF 底层驱动与 Scanner 数据入口审计

> 目标：在推进 Agent B 扫描器移植前，把目标板所有可用的底层硬件/无线入口拆清楚。原则仍然是 **尽量直接使用 Waveshare 官方 ESP-IDF 例程和 Espressif 官方组件，不重写驱动，不发明新的 packet framework**。Agent D 的成果是让 Agent B 能明确知道“从哪里拿数据、拿到什么原生结构、哪些回调在 ISR/高优先级上下文、怎样最薄地送入扫描器”。

## 0. 与其他 Agent 的边界

```text
Agent D
硬件 / ESP-IDF 原生驱动 / RF入口
        ↓
原始 frame / advertisement / scan record / socket data
        ↓
Agent B
扫描 / 协议发现 / parser
        ↓
Agent C
设备识别数据库
        ↓
Agent A
HA Device / Entity / State
```

Agent D **不负责**：

- Bettercap/Wireshark/Kismet 等 scanner 逻辑（Agent B）
- HA/ZHA/ESPHome 识别数据库（Agent C）
- Device/Entity/State（Agent A）
- 新建通用 packet abstraction

Agent D 只负责：**把目标板和 ESP-IDF 能提供的原生数据入口找全、验证、整理成 B 线可直接消费的接口地图。**

---

## 1. 第一优先级上游

### 1.1 Waveshare 官方板级仓库

Repository:

```text
waveshareteam/ESP32-C6-LCD-1.9
```

官方仓库明确同时支持：

- ESP32-C6-LCD-1.9
- ESP32-C6-Touch-LCD-1.9

并且存在独立 `02_Example/ESP-IDF/`，当前可见：

```text
01_ADC_Test
02_I2C_QMI8658
03_SD_Card
04_WS2812_Test
05_WIFI_AP
06_WIFI_STA
07_LVGL_Test
08_FactoryProgram
```

**优先直接复制这些 ESP-IDF 例程中的 board bring-up / GPIO / SPI / I2C / SD / LVGL 代码，避免自己重新写 BSP。**

### 1.2 Espressif ESP-IDF

重点组件/例程：

```text
components/esp_wifi
examples/network/simple_sniffer

components/bt/host/nimble
examples/bluetooth/nimble/*

components/ieee802154
examples/ieee802154/*

components/openthread
components/esp-zigbee / ESP Zigbee SDK (外部组件)

lwIP
esp_netif
mdns
FATFS / SDSPI
esp_lcd
```

优先复用官方 public API 和官方 example；不要从 private driver 复制，除非 public API 无法完成目标，并且必须在报告中明确标注风险。

---

## 2. Agent D 必须交付的“数据入口矩阵”

最终至少输出如下矩阵：

| Transport | ESP-IDF 原生入口 | 原生数据结构/回调 | 可给 B 的数据 | 是否 ISR/高优先级 | 是否可直接复制官方 example |
|---|---|---|---|---|---|
| Wi-Fi active scan | `esp_wifi_scan_start/get_ap_records` | `wifi_ap_record_t` | SSID/BSSID/RSSI/channel/auth/capability | 否 | 是 |
| Wi-Fi passive/raw | `esp_wifi_set_promiscuous_rx_cb` | `wifi_promiscuous_pkt_t`, `wifi_pkt_rx_ctrl_t` | 802.11 raw MPDU + RSSI/channel/rate metadata | callback context，必须轻量 | 是，`simple_sniffer` |
| BLE discovery | ESP-NimBLE GAP scan | NimBLE GAP discovery event / advertisement fields | addr/RSSI/name/UUID/manufacturer/service data | NimBLE host context | 是 |
| BLE interaction | ESP-NimBLE GATT client | GATT discovery/read/write callbacks | service/char/value | NimBLE host context | 是 |
| IEEE 802.15.4 raw | `esp_ieee802154_*` | `esp_ieee802154_receive_done(frame, frame_info)` | raw 802.15.4 MAC frame + RSSI/LQI/channel | **ISR context** | 是，802.15.4 CLI/example |
| IEEE 802.15.4 scan | channel + promiscuous + receive loop / energy detect | `esp_ieee802154_frame_info_t` | Zigbee/Thread candidate frames | ISR + worker | 官方 API组合 |
| LAN/IP | `esp_netif` + lwIP socket/raw APIs | IP/UDP/TCP/socket data | mDNS/SSDP/DHCP/HTTP 等 B 层输入 | task context | 是 |
| SD | SDSPI + FATFS | file API | C数据库、可选 capture/debug | task context | 是，Waveshare `03_SD_Card` |
| Display | `esp_lcd` + LVGL | panel/LVGL callbacks | UI only | task/DMA callback | 是 |
| Touch | Waveshare CST816 I2C BSP | x/y/touch state | UI only | task/I2C | 是 |
| IMU | QMI8658 I2C | accel/gyro | 可选 UI/姿态，不进入 scanner 主线 | task | 是 |

Agent D 要把上表中的“推测”全部替换成 **精确 API 名称、header、example 路径、字段、限制、RAM 成本和生命周期**。

---

## 3. Wi-Fi：B 线最重要的第一个数据入口

优先确认两条独立路径。

### A. 官方 active scan

目标：直接得到网络/AP 设备候选。

优先使用 ESP-IDF 自带：

```text
esp_wifi_scan_start()
esp_wifi_scan_get_ap_records()
```

输出尽量直接保留 `wifi_ap_record_t`，Agent B 再决定是否解析成 HA discovery candidate。

### B. Promiscuous / passive frame RX

ESP-IDF public API 已提供：

```text
esp_wifi_set_promiscuous_rx_cb()
esp_wifi_set_promiscuous_filter()
esp_wifi_set_promiscuous(true)
esp_wifi_set_channel()
```

官方 `examples/network/simple_sniffer/main/cmd_sniffer.c` 已经包含完整启动顺序，Agent D 应把这一段列为 **首选直接复制候选**，而不是自行重新实现 sniffer 初始化。

Agent D 要确认：

- `wifi_promiscuous_pkt_t` payload 长度/边界
- `wifi_pkt_rx_ctrl_t` 可用 metadata
- management/control/data frame filter mask
- callback context / 禁止在 callback 内做哪些重活
- channel hopping 对 STA/AP 模式的影响
- scan 与 promiscuous 是否可切换/并行
- Wi-Fi 6 / 802.11ax frame 元数据在 C6 上能拿到多少
- 是否可在不复制 payload 的情况下短暂解析 header；若必须跨 task，最小复制策略是什么

目标不是写新的 packet object，而是：

```text
ESP-IDF wifi_promiscuous_pkt_t
       -> minimal queue/ring
       -> Agent B parser
```

---

## 4. BLE：尽量直接喂 HA Bluetooth 风格 discovery

底层优先 ESP-NimBLE，不使用 Arduino BLE wrapper。

Agent D 要定位：

- GAP scan start/stop API
- legacy advertisement 与 extended advertisement
- discovery event 中：address / address type / RSSI / flags / local name / service UUID / manufacturer data / service data / TX power
- duplicate filtering 行为
- active scan / passive scan 的差异与 RAM/功耗
- scanner restart / radio scheduler 下的生命周期
- GATT client 最小 bring-up 路径

理想输出：**不要设计 NearBy BLE packet struct**。

尽可能让 Agent B 从 NimBLE 原始 event 直接生成 HA Bluetooth-compatible service info。

---

## 5. IEEE 802.15.4：C6 的关键优势

ESP-IDF 当前 public `esp_ieee802154.h` 已明确提供：

```text
esp_ieee802154_enable()
esp_ieee802154_disable()
esp_ieee802154_set_channel(11..26)
esp_ieee802154_set_promiscuous(true)
esp_ieee802154_receive()
esp_ieee802154_energy_detect()
```

以及用户实现的接收事件：

```text
esp_ieee802154_receive_done(
    uint8_t *frame,
    esp_ieee802154_frame_info_t *frame_info
)
```

官方 header 明确说明：

- receive event 在 **ISR context**
- `frame` 是长度字节 + MHR + MAC Payload，无 FCS
- driver 会校验 FCS
- 接收 frame 处理后必须调用 `esp_ieee802154_receive_handle_done(frame)`
- frame info 提供额外接收信息

这意味着 D 线必须认真设计 **最薄的 ISR→worker handoff**，但不要发明复杂框架。

建议优先研究是否可以直接复用 ESP-IDF `examples/ieee802154/ieee802154_cli` / debug component 的收包模式。

Agent D 要给 B 明确回答：

```text
raw IEEE 802.15.4
  -> 能否在 promiscuous 下拿到 Zigbee / Thread MAC frames
  -> frame metadata 有哪些
  -> 多久必须 receive_handle_done
  -> 是否需要立即 memcpy
  -> 最小 buffer pool 大小
  -> channel hopping 如何切
```

Zigbee/Thread protocol stack 本身归后续 B/Integration 研究；D 只确保 radio RX 入口可用。

---

## 6. RF 共存 / Radio Scheduler 必须由 D 给出硬事实

ESP32-C6 Wi-Fi / BLE / IEEE 802.15.4 共用 2.4GHz RF。

Agent D 要从 ESP-IDF coexistence 文档和代码确认：

- 哪些组合官方支持软件 coexistence
- promiscuous Wi-Fi 与 BLE scan 的实际限制
- Wi-Fi 与 802.15.4 raw RX 是否能同时保持 initialized
- 切换时哪些 stack 可以保留、哪些必须 stop/disable
- 初始化后的静态 RAM 占用
- mode switch 的典型耗时
- 对扫描器最安全的状态机

D 不需要发明高层 scheduler，只要输出一张 **可行状态转换表** 给 B：

```text
WIFI_SCAN
WIFI_PROMISC
BLE_SCAN
BLE_GATT
I154_PROMISC
ZIGBEE_FOCUS
THREAD_FOCUS
WIFI_LAN_FOCUS
```

并标出合法/非法切换、需要 stop 的 driver。

---

## 7. LAN/IP：只做底座，mDNS/SSDP/DHCP 属于 B

D 负责：

```text
Wi-Fi STA/AP
esp_netif
lwIP
socket
multicast/broadcast 能力
```

B 负责：

```text
mDNS
SSDP
DHCP fingerprint
HTTP discovery
Responder 风格 LAN fingerprint
```

D 要确认：

- STA 连接期间是否还能做 promiscuous/扫描，限制是什么
- multicast UDP socket 使用方式
- 255.255.255.255 broadcast
- IPv4/IPv6
- lwIP raw API 是否值得用，还是普通 UDP/TCP socket 最省代码

原则仍然是：**能用普通 socket 就不写 raw lwIP glue。**

---

## 8. SD：Agent C 数据库的硬件基础

Waveshare 官方 ESP-IDF LVGL 示例显示 SD 与 LCD 共用 `SPI2_HOST`，示例中：

```text
LCD MOSI GPIO4
LCD CLK  GPIO5
LCD CS   GPIO7
LCD DC   GPIO6
LCD RST  GPIO14

SD MISO  GPIO19
SD CS    GPIO20
```

并直接使用：

```text
SDSPI_HOST_DEFAULT()
esp_vfs_fat_sdspi_mount()
```

Agent D 必须进一步确认正式原理图上的 pin mapping，并检查：

- LCD + SD 共 SPI bus 时 transaction locking
- LVGL DMA 刷屏期间 SD 随机读取延迟
- 1GB FAT/FAT32 挂载
- 数据库读取建议的 block size
- 是否值得为 C 的数据库保留 file handle/cache

这里优先复制 Waveshare `03_SD_Card` 和 LVGL 示例，不写新 SD driver。

---

## 9. Display / Touch：直接复用 Waveshare，但必须验证官方例程一致性

官方 Waveshare 文档当前明确写的是：

```text
LCD: ST7789V2
170x320
SPI
Touch: CST816
I2C
```

而 Waveshare GitHub 当前 `07_LVGL_Test/main/main.c` 却包含：

```text
#include "esp_lcd_sh8601.h"
esp_lcd_new_panel_sh8601(...)
```

并使用一组看起来像 AMOLED/SH8601 风格的 vendor init 接口。

**这是 Agent D 必须优先核实的异常点。** 不要直接把这个 LVGL 示例当成最终真值。

判断优先级：

1. 目标板实物/官方 schematic + docs
2. 当前板型官方资源页的 ST7789V2/CST816 datasheet
3. Waveshare repo 对应 touch board 的实际工作 example
4. Espressif `esp_lcd` ST7789 官方 component/example

Touch BSP 当前 repo 中可看到：

```text
CST816 address = 0x15
I2C0
SCL = GPIO8
SDA = GPIO18
```

`touch_bsp.c` 直接读取 0x00 开始的 7 字节并解析 x/y。若实物验证一致，这部分可直接复制。

---

## 10. 内存审计是 Agent D 的强制交付物

目标板只有约 512KB HP SRAM，没有可依赖的大 PSRAM。

D 必须做一张实测/编译 map 表：

| 模块 | init 前 free heap | init 后 free heap | static/BSS | task stack | RX buffers | 可否 deinit 回收 |
|---|---:|---:|---:|---:|---:|---|
| Wi-Fi | | | | | | |
| NimBLE | | | | | | |
| IEEE802154 | | | | | | |
| LVGL | | | | | | |
| SD/FATFS | | | | | | |
| Zigbee stack candidate | | | | | | |
| OpenThread candidate | | | | | | |

这一张表将直接决定 B 的 radio mode 是否必须动态初始化/释放。

---

## 11. Agent D 的最终 deliverables

Agent D 分支至少应形成：

```text
docs/research/agent-d-hardware-driver-audit.md

docs/hardware/
  board-pin-map.md
  esp-idf-radio-entrypoints.md
  rf-coexistence-matrix.md
  memory-budget.md
  scanner-handoff-contract.md
```

若开始落代码，优先：

```text
components/board/
```

只封装 Waveshare 必须的 board bring-up；无线 driver 尽量仍让 B 直接调用 ESP-IDF public API。

`scanner-handoff-contract.md` 不应设计新的 packet schema，而应规定：

```text
Wi-Fi -> ESP-IDF native packet + minimal lifetime rule
BLE   -> NimBLE native advertisement event + lifetime rule
15.4  -> ESP-IDF frame/frame_info + ISR copy/release rule
LAN   -> lwIP socket payload
```

如果确实需要跨 task 保存数据，允许非常薄的 fixed-size envelope/ring descriptor，但必须证明是生命周期所迫，而不是为了抽象而抽象。

---

## 12. 推荐执行顺序

```text
D1  Waveshare schematic/repo pin + BSP 审计
D2  Wi-Fi active scan + promiscuous 官方 example 验证
D3  NimBLE passive scan 验证
D4  IEEE 802.15.4 promiscuous RX 验证
D5  RF coexistence / mode switch 验证
D6  SD + LVGL 同 SPI 压力测试
D7  heap / stack / buffer 实测
D8  输出给 Agent B 的 handoff matrix
```

### 第一阶段成功标准

不用任何 Bettercap/Kismet/HA 代码，只靠 ESP-IDF/Waveshare 上游，能在串口证明三个独立入口：

```text
Wi-Fi raw frame -> type / BSSID / RSSI / channel
BLE advertisement -> addr / RSSI / UUID / manufacturer data
802.15.4 frame -> length / RSSI / LQI / channel / MAC header
```

然后把这些原生数据入口交给 Agent B。

这一步通过后，B 才开始真正裁 scanner/parser。