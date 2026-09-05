# Agent B — 扫描/抓包参考项目拆解与最大复用路线

> 目标：为 ESP32-C6 的 Wi-Fi / BLE / LAN / 802.15.4 discovery 层尽量寻找可直接复制、可裁剪或可作为字段真值的成熟代码。NearBy One NEXT 只吸收发现、监听、协议解析、fingerprint、packet/event model 等能力；攻击、欺骗、劫持、凭据获取链路不进入产品主线。
>
> 本轮覆盖：Bettercap、Wireshark、Scapy、Kismet、Aircrack-ng、hcxdumptool、mitmproxy、BtleJack、nRF Sniffer、Responder。

## 0. 结论先行

如果目标是“尽量复制粘贴”，这 10 类参考项目不能等价看待。

### 最值得直接搬的候选

1. **hcxdumptool** — C + MIT；Wi-Fi frame/IE/PCAPNG 周边是当前最好的直接裁剪候选之一。
2. **BtleJack** — MIT；host 为 Python，但存在独立 firmware 项目，可重点拆 BLE channel/packet/state 处理思想和小型协议代码。
3. **mitmproxy** — MIT，但 Python/桌面依赖重；仅适合拿 HTTP flow/message 数据模型和少量纯 parser 思路，优先级低于 ESP-IDF 自带 HTTP/LwIP 能力。

### 最值得当“协议真值/识别逻辑参考”的候选

4. **Wireshark** — dissector 覆盖极广，是字段定义/解析顺序/异常包处理的最佳参考，但代码体量和依赖很重且 GPL。
5. **Scapy** — packet layer 定义清晰，非常适合当协议结构参考和 PC 端测试向量生成器，但 Python/GPL，不适合 MCU runtime。
6. **Kismet** — device inventory、802.11 设备归类、SSID/WPS/OUI 等逻辑很强，适合作为 scanner -> device observation 设计参考，但 C++ 大型运行时不适合搬。
7. **Bettercap** — Wi-Fi/BLE recon session、event、channel hopping、设备列表组织方式值得借鉴；Go runtime 和 GPLv3 决定了直接搬 MCU 性价比低。
8. **Aircrack-ng** — `airodump-ng` 是 Wi-Fi AP/station inventory 的重要参考；C 代码可读，但 Linux `osdep` 和 GPLv2 需要隔离评估。
9. **Responder** — 只参考 LLMNR/NBT-NS/mDNS/服务报文解析与 LAN 主机识别；poisoning/rogue auth 全部排除。
10. **nRF Sniffer for Bluetooth LE** — 官方行为模型很值得参考，但本轮未找到当前官方 BLE Sniffer 的 canonical public source repo；Nordic SDK 存在仅限 Nordic IC 的许可证条款，因此先标为 **reference-only**，不能默认复制到 ESP32。

---

## 1. 复用矩阵

| 项目 | 主要语言 | 当前许可/状态 | NearBy 最值得拿的部分 | 直接复制优先级 |
|---|---|---|---|---|
| Bettercap | Go | GPLv3 | Wi-Fi/BLE recon 生命周期、channel hopping、session/event/device inventory | C |
| Wireshark | C | GPLv2 | 802.11/BLE/802.15.4/DNS 等 dissector 的字段解析真值 | C（技术高、搬运低） |
| Scapy | Python | GPLv2 | packet layer 定义、TLV/IE 结构、PC 端测试向量 | D（runtime）/ A（测试参考） |
| Kismet | C++ | custom/mixed，逐文件核对 | device tracker、802.11 PHY 分类、SSID/WPS/OUI | C |
| Aircrack-ng | C | GPLv2 | airodump AP/station inventory、radiotap/802.11 parsing、OUI flow | B/C |
| hcxdumptool | C | MIT | 802.11 structs、frame parsing、channel/capture loop、PCAPNG | **A** |
| mitmproxy | Python | MIT | HTTP flow/message model、解析/展示思想 | D |
| BtleJack | Python + firmware | MIT | BLE packet/channel/state、sniffer framing、firmware-side logic | **A/B** |
| nRF Sniffer BLE | tool + firmware | source/license需单独确认 | BLE advertising discovery、follow/filter、sniffer UX | Reference only |
| Responder | Python | GPLv3 | LLMNR/NBT-NS/mDNS packet/service identification | C |

> 等级：A=优先直接裁剪；B=少量重用；C=主要借鉴；D=只作为测试/行为参考。

---

## 2. Bettercap

Repository: `bettercap/bettercap`

已定位关键源码：

- `modules/wifi/wifi.go`
- `modules/ble/ble_recon.go`
- session/module handler 周边

### 值得拿

#### Wi-Fi recon 状态机

`wifi.recon` 已经有成熟的：

- start/stop
- channel selection
- channel hopping
- probe handling
- AP/station inventory
- session handler

ESP32-C6 不需要复制 Go 实现，但可以一比一照其生命周期切出非常薄的 C scanner：

```text
scanner_start
scanner_stop
set_channels
on_packet
emit_observation
```

#### BLE recon

`modules/ble/ble_recon.go` 把 BLE discovery 当成独立 recon module，并维护 RSSI/MAC/seen 等信息。

这与 NearBy 的 transient session 非常契合。

### 不拿

- MITM/spoofing/offensive modules
- Go runtime/session framework 本体
- OS-specific packet capture

### 许可证

Bettercap 当前为 GPLv3。若直接复制代码进入固件，必须先决定 NearBy 的最终许可证策略；否则只借鉴结构。

---

## 3. Wireshark

Repository: `wireshark/wireshark`

License: GPLv2。

### 核心价值

Wireshark 对我们最大的价值不是 GUI，而是 dissector。

重点 source map：

```text
epan/dissectors/packet-ieee80211.c
epan/dissectors/packet-btle.c
epan/dissectors/packet-ieee802154.c
epan/dissectors/packet-dns.c
```

按后续实际协议再追：

- mDNS/DNS-SD
- DHCP
- SSDP/HTTP
- Zigbee/ZCL
- Thread/6LoWPAN/CoAP
- Matter

### 推荐用法

#### A. 协议字段真值

当 ESP-IDF 给到 raw frame，而某字段不知道 offset/长度/可选条件时：

1. 先看 Wireshark dissector。
2. 再看标准/ESP-IDF 定义。
3. 只裁剪 NearBy 实际需要的最小解析路径。

#### B. 测试 Oracle

PC 端将同一 PCAP：

```text
Wireshark/tshark result
vs
NearBy parser result
```

做自动对比。

### 为什么不建议整体搬 dissector

- GLib/Wireshark runtime 依赖重
- proto tree / tvbuff / expert info 大量桌面框架耦合
- 目标板 RAM 很小
- GPL 许可需要单独处理

结论：**Wireshark 是第一协议参考库，不是第一复制库。**

---

## 4. Scapy

Repository: `secdev/scapy`

License metadata: GPL-2.0。

### 值得拿的 source map

```text
scapy/layers/dot11.py
scapy/layers/bluetooth.py
scapy/layers/dns.py
scapy/layers/inet.py
scapy/layers/inet6.py
```

### 最适合 NearBy 的用途

不是移植 Python，而是：

#### 1. Packet schema 参考

Scapy 把字段和层次写得比很多 C 项目更直观，非常适合确认：

- 802.11 management frame
- Dot11Elt
- BLE advertising payload
- DNS/mDNS
- IPv6/ICMPv6

#### 2. PC 端测试包生成器

这是 Scapy 对 NearBy 的高价值用途：

```text
Scapy 构造 packet
 -> 保存 test vector / pcap
 -> ESP32 parser 单测
```

这样可大量减少我们自己手写 fuzz/test packet 的代码。

### 不搬

- Scapy runtime
- Python dynamic packet classes
- socket abstraction

---

## 5. Kismet

Repository: `kismetwireless/kismet`

关键源码已定位：

- `phy_80211.h`
- `phy_80211_components.cc`
- device tracker / PHY 相关代码

### 最值得借鉴

#### 1. 从 packet 到“现实设备”的整理

Kismet 很强的一点是它不是单纯列包，而是不断维护 device inventory。

我们第 2 层 scanner 输出给 HA 层前，需要的 observation 组织方式可以参考它：

```text
MAC/BSSID
SSID
channel
RSSI
first/last seen
WPS fields
vendor/OUI
role hint
```

#### 2. 802.11 WPS / SSID / manufacturer 信息

`phy_80211.h` 中能看到 WPS device/model 等设备信息字段，这些对于把匿名 BSSID 进一步识别成现实设备很有价值。

#### 3. OUI

Kismet 明确使用 OUI 数据做 manufacturer identification。

NearBy 可以直接采用 IEEE OUI 数据生成只读 prefix table，而不是重新维护厂商列表。

### 不建议直接搬

- 全部 C++ tracker framework
- protobuf/web server/database
- alert/IDS runtime

---

## 6. Aircrack-ng / airodump-ng

Repository: `aircrack-ng/aircrack-ng`

License metadata: GPL-2.0。

核心 source map：

```text
src/airodump-ng/airodump-ng.c
src/airodump-ng/dump_write.c
scripts/airodump-ng-oui-update
```

### 值得拿

`airodump-ng` 的产品数据结构与 NearBy Wi-Fi discovery 非常接近：

- AP inventory
- station inventory
- BSSID/ESSID
- channel
- security summary
- packet counters
- signal
- probe SSID

因此第一个 Wi-Fi scanner 的字段列表应优先对照 `airodump-ng`，而不是自己想一套。

### 需要剥离

- `osdep` Linux capture abstraction
- monitor-mode interface setup
- injection/aireplay 路径
- cracking/handshake pipeline

ESP32 输入直接来自 `esp_wifi_set_promiscuous_rx_cb()` 等 ESP-IDF API。

### OUI

`airodump-ng-oui-update` 明确以 IEEE OUI 数据生成厂商列表。我们应直接做构建期 OUI table，不维护手写 vendor map。

---

## 7. hcxdumptool — Wi-Fi 直接裁剪优先级最高

Repository: `ZerBea/hcxdumptool`

Language: C
License: MIT

当前仓库结构非常适合拆：

```text
hcxdumptool.c
include/
docs/
```

### 为什么它比 Aircrack/Bettercap 更适合“复制粘贴优先”

- C
- 体量相对集中
- MIT
- 大量 802.11 low-level struct/constant/parsing
- capture/channel/PCAPNG 逻辑集中

### 优先拆的内容

1. IEEE 802.11 frame structs
2. management frame subtype handling
3. information element walking
4. MAC address extraction
5. channel/frequency tables
6. packet counters/seen tables的最小部分
7. PCAPNG block structs/writer（以后手动 export capture 时）

### 必须丢掉/不进入主线

- deauthentication/injection
- PMKID/handshake harvesting
- credential-oriented capture logic
- Linux raw socket / nl80211 control

### ESP32 替换点

```text
Linux socket RX         -> ESP-IDF promiscuous callback
nl80211 channel control -> esp_wifi_set_channel
clock/system APIs       -> esp_timer / FreeRTOS
file output             -> 可选 FATFS/SD，v0.1 不启用
```

这是 Agent B 下一步最应该先实际裁代码的项目。

---

## 8. mitmproxy

Repository: `mitmproxy/mitmproxy`

Language: Python
License: MIT

### 对 NearBy 有价值的部分

只保留：

- HTTP request/response/flow 的数据组织思路
- protocol inspector UX
- headers/body metadata representation

### 优先级低

NearBy 的产品目标是附近设备 discovery + HA Entity，不是代理服务器。

ESP-IDF 已有 HTTP client/server、TLS、lwIP；为了“不写新功能”，不要为了参考 mitmproxy 而实现 proxy/CA/TLS interception。

### 明确不做

- TLS MITM
- certificate injection
- transparent proxy
- credential interception

---

## 9. BtleJack

Repository: `virtualabs/btlejack`

License: MIT

仓库包含：

- Python host package `btlejack/`
- `btlejack-firmware` 子模块

### 值得拆

- BLE advertising/connection packet framing
- channel concepts
- packet/state representation
- host <-> sniffer firmware framing
- firmware 中小型 BLE radio state handling

### ESP32 上的现实边界

ESP32-C6 的 BLE Controller/NimBLE 已经负责大量链路层工作，所以不要为了“像 BtleJack”而绕开 Espressif controller 重写 BLE radio。

主要用途应是：

```text
BtleJack packet/state definitions
 -> 对照 NimBLE scan/raw event
 -> 补充 NearBy discovery metadata
```

攻击/劫持相关功能不进入产品主线。

---

## 10. nRF Sniffer for Bluetooth LE

当前 Nordic 官方把 nRF Sniffer for BLE 作为 Wireshark external capture / nRF Util 工具提供，可列出附近 advertising devices，并提供 address、address type、name、RSSI 等。

### 值得借鉴

其“用户可见 discovery minimum set”很适合 NearBy BLE scanner：

```text
address
address type
name
RSSI
advertising data
```

### 当前处理决定

本轮没有找到可确认的当前官方 BLE Sniffer canonical public source repo。

同时 Nordic SDK 中存在 `LicenseRef-Nordic-5-Clause` 代码，包含“软件仅可用于 Nordic Semiconductor IC”的硬件限制。因此：

**在明确到具体 sniffer source file 和许可证之前，nRF Sniffer 一律 reference-only，不复制其受限代码到 ESP32-C6。**

这不影响我们参考其行为、UI 和公开协议描述。

---

## 11. Responder

Repository: `lgandx/Responder`

License metadata: GPLv3。

### 只拿 LAN discovery 相关知识

重点理解：

- LLMNR
- NBT-NS
- mDNS
- HTTP/SMB 等服务存在性的 packet fingerprint

NearBy LAN scanner 可以利用被动看到的：

```text
hostname
service name
IPv4/IPv6
MAC
protocol/service hint
```

交给 HA discovery matcher。

### 明确排除

- LLMNR/NBT-NS poisoning
- rogue authentication servers
- NTLM credential capture
- SMB auth interception

最佳做法是参考 packet format，然后优先使用 lwIP/mDNS/ESP-IDF 的正常 discovery API，而不是移植 Responder runtime。

---

## 12. 推荐的 scanner 层，不增加新框架

只定义最小输入输出：

```text
ESP-IDF driver/event
      |
      v
scanner-specific parsing
      |
      v
HA-compatible discovery input
```

### BLE scanner 输出

尽量直接对应 HA Bluetooth matcher 能消费的信息：

```text
address
name/local_name
RSSI
service_uuids
service_data
manufacturer_data
connectable
```

### Wi-Fi scanner 输出

HA 没有一个等价的 raw 802.11 matcher，所以 Wi-Fi scanner 主要用于：

- AP/device card discovery
- vendor/OUI
- 后续 LAN 关联
- mDNS/DHCP/SSDP discovery 的网络入口

不要自己做庞大的“Wi-Fi Entity 类型系统”。

### LAN scanner 输出

直接喂 HA-style discovery：

```text
zeroconf
ssdp
dhcp
homekit
```

---

## 13. 第一个真实复制任务建议

### Task B1 — hcxdumptool passive 802.11 parser extraction

目标不是先写 scanner，而是**从 MIT C 代码中直接裁出可编译的小块**。

输出建议：

```text
components/scanners/wifi/hcx/
  ieee80211.h
  ieee80211_parse.c
  ieee80211_parse.h
  NOTICE
```

只保留：

- frame header structs
- frame-control/subtype helpers
- management IE walker
- SSID/channel/security/vendor IE extraction

输入：

```text
const uint8_t *frame
size_t len
RSSI/channel metadata from ESP-IDF
```

输出给 UI 之前仍然经过 HA/Device/Entity 层；scanner 本身不画卡片。

### Task B2 — airodump/Kismet 字段对照测试

建立 Wi-Fi observation 的 regression fixtures，验证我们从同一个 beacon/probe frame 得到：

- SSID
- BSSID
- channel
- privacy/security hint
- manufacturer/OUI

与成熟工具一致。

### Task B3 — BLE

优先直接用 ESP-IDF/NimBLE scan structures；BtleJack/nRF Sniffer 主要做字段/state 参考，不先重写 Link Layer。

---

## 14. License gate — “复制粘贴优先”必须先过这一关

NearBy 现在还没有最终开源许可证决定，因此不要把所有项目代码直接混进 main。

建议所有 source port 都采用：

```text
third_party source
 -> isolated branch/commit
 -> record original license
 -> record original file + commit
 -> decide compatible/incompatible
 -> only then merge
```

### 当前明显较容易的

- Home Assistant Core — Apache-2.0
- hcxdumptool — MIT
- BtleJack — MIT
- mitmproxy — MIT

### 需要项目级许可证决策的

- Bettercap — GPLv3
- Wireshark — GPLv2
- Scapy — GPLv2
- Aircrack-ng — GPLv2
- Responder — GPLv3
- Kismet — repository metadata 无单一 SPDX，需要逐文件确认
- Nordic code — 需逐文件确认，部分 Nordic 5-Clause 代码有硬件限制

这不是法律意见；合并实际代码前需按具体文件核对 license/header。

---

## 15. Agent B 后续执行清单

- [ ] 从 hcxdumptool 抽 passive-only 802.11 struct/parser 候选清单
- [ ] 标记每个候选函数的 Linux API 依赖
- [ ] 生成 `Linux API -> ESP-IDF API` 替换表
- [ ] 对照 airodump-ng 的 AP/station inventory 字段
- [ ] 对照 Kismet 的 WPS/OUI/device fields
- [ ] 建 Wireshark dissector source index：Wi-Fi/BLE/802.15.4/Zigbee/Thread/mDNS/DHCP/SSDP
- [ ] 用 Scapy 生成 parser test vectors/pcaps
- [ ] 拆 BtleJack firmware，只保留 discovery/sniffer 相关部分
- [ ] 核查 nRF Sniffer BLE 当前 source package 的具体许可
- [ ] 只提取 Responder 的协议解析知识，禁止 poisoning/auth capture code
- [ ] 建立 `THIRD_PARTY.md` source provenance 表

## Source map

- https://github.com/bettercap/bettercap
  - `modules/wifi/wifi.go`
  - `modules/ble/ble_recon.go`
- https://github.com/wireshark/wireshark
  - `epan/dissectors/packet-ieee80211.c`
  - `epan/dissectors/packet-btle.c`
  - `epan/dissectors/packet-ieee802154.c`
  - `epan/dissectors/packet-dns.c`
- https://github.com/secdev/scapy
  - `scapy/layers/dot11.py`
  - `scapy/layers/bluetooth.py`
  - `scapy/layers/dns.py`
- https://github.com/kismetwireless/kismet
  - `phy_80211.h`
  - `phy_80211_components.cc`
- https://github.com/aircrack-ng/aircrack-ng
  - `src/airodump-ng/airodump-ng.c`
  - `src/airodump-ng/dump_write.c`
  - `scripts/airodump-ng-oui-update`
- https://github.com/ZerBea/hcxdumptool
  - `hcxdumptool.c`
  - `include/`
- https://github.com/mitmproxy/mitmproxy
- https://github.com/virtualabs/btlejack
  - `btlejack/`
  - `btlejack-firmware` submodule
- Nordic nRF Sniffer for Bluetooth LE official documentation / nRF Util distribution
- https://github.com/lgandx/Responder
