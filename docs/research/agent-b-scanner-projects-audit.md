# Agent B — 扫描/抓包参考项目拆解与最大复用路线

> 目标：为 ESP32-C6 的 Wi-Fi / BLE / LAN / 802.15.4 discovery 层尽量寻找可直接复制、可裁剪或可作为字段真值的成熟代码。NearBy One NEXT 只吸收发现、监听、协议解析、fingerprint、packet/event model 等能力；攻击、欺骗、劫持、凭据获取链路不进入产品主线。
>
> 本轮覆盖：Bettercap、Wireshark、Scapy、Kismet、Aircrack-ng、hcxdumptool、mitmproxy、BtleJack、nRF Sniffer、Responder，以及 Home Assistant 原生 Bluetooth/Zeroconf/SSDP/DHCP discovery。
>
> **前置依赖：Agent D。** B 线正式裁 scanner/parser 前，先以 Agent D 对 Waveshare/ESP-IDF 的 Wi-Fi promiscuous、NimBLE GAP、IEEE 802.15.4 raw RX、lwIP/esp_netif、RF coexistence 和 frame lifetime 审计为底层真值。B 不自行重写底层驱动。

## 0. 结论先行

如果目标是“尽量复制粘贴”，这些参考项目不能等价看待。

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
10. **nRF Sniffer for Bluetooth LE** — 官方行为模型很值得参考，但 source/license 需单独核对，默认 reference-only。
11. **Home Assistant native discovery** — Bluetooth manager/matcher、Zeroconf、SSDP、DHCP 的工程化 discovery lifecycle 与匹配输入格式，作为 B 线正式参考源。

---

## 1. Agent D -> Agent B 输入边界

B 不再负责获取 RF 原始数据；它应直接消费 D 确认过的官方原生入口：

```text
Wi-Fi active scan
  wifi_ap_record_t

Wi-Fi passive/raw
  wifi_promiscuous_pkt_t
  wifi_pkt_rx_ctrl_t

BLE
  ESP-NimBLE GAP discovery event / advertisement payload

IEEE 802.15.4
  frame + esp_ieee802154_frame_info_t

LAN/IP
  lwIP/BSD socket payload
```

原则：**不要先发明 NearBy packet schema 再解析。** 能直接以 ESP-IDF/NimBLE native struct 输入 parser 就直接用。只有 ISR/task 生命周期要求跨 task 保存时，才允许非常薄的 fixed-size envelope/ring descriptor。

完整链路：

```text
Agent D
hardware / ESP-IDF RX
        ↓
Agent B
scanner / dissector / discovery
        ↓
Agent C
recognition database
        ↓
Agent A
HA Device / Entity / State
```

---

## 2. 复用矩阵

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
| Home Assistant discovery | Python | HA Core Apache-2.0；requirements单独核对 | Bluetooth/Zeroconf/SSDP/DHCP scanner lifecycle + service-info normalization | **A/B** |

> 等级：A=优先直接裁剪；B=少量重用；C=主要借鉴；D=只作为测试/行为参考。

---

## 3. Bettercap

Repository: `bettercap/bettercap`

重点参考：Wi-Fi/BLE recon 状态机、start/stop、channel selection/hopping、session/event/device inventory。不要搬 Go runtime 或攻击模块。

---

## 4. Wireshark

Repository: `wireshark/wireshark`，GPLv2。

重点：

```text
epan/dissectors/packet-ieee80211.c
epan/dissectors/packet-btle.c
epan/dissectors/packet-ieee802154.c
epan/dissectors/packet-dns.c
```

用途主要是字段真值和 PC 端 oracle，不整体搬 runtime。

---

## 5. Scapy

Repository: `secdev/scapy`，GPLv2。

重点当 packet schema 参考和 PC 端测试向量生成器，不移植 Python runtime。

---

## 6. Kismet

Repository: `kismetwireless/kismet`。

重点：RF device inventory、AP/station role、SSID/WPS/OUI、RSSI/channel 观察模型；不搬大型 C++ tracker/web/database runtime。

---

## 7. Aircrack-ng / airodump-ng

Repository: `aircrack-ng/aircrack-ng`，GPLv2。

重点：AP/station inventory、802.11 management frame parsing、channel/security/probe 信息；Linux `osdep` 与 injection/cracking 路径剥离。

---

## 8. hcxdumptool

Repository: `ZerBea/hcxdumptool`，C + MIT。

当前最优先的 Wi-Fi 直接裁剪候选：

- IEEE 802.11 frame structs
- management subtype handling
- information element walking
- MAC extraction
- channel/frequency tables
- PCAPNG structs/writer（可选）

底层 RX/频道切换全部改由 Agent D 确认的 ESP-IDF public API 提供。

明确排除：deauth/injection、PMKID/handshake harvesting、credential-oriented flows。

---

## 9. mitmproxy

只参考 HTTP flow/message model 和 inspector UX；不做 TLS MITM、证书注入、transparent proxy。

---

## 10. BtleJack

MIT。重点拆 BLE packet/channel/state 与 sniffer framing；不做 hijacking。

---

## 11. nRF Sniffer BLE

参考 BLE advertising discovery、packet follow/filter 和 sniffer UX；许可证/官方 canonical source 未完全确认前保持 reference-only。

---

## 12. Responder

只参考 LLMNR/NBT-NS/mDNS/service fingerprint；不做 poisoning、rogue auth、credential capture。

---

## 13. Home Assistant native scanner/discovery

HA 原生扫描能力纳入 B 线正式参考源。

### Bluetooth

重点：

```text
homeassistant/components/bluetooth/manager.py
homeassistant/components/bluetooth/match.py
homeassistant/components/bluetooth/passive_update_processor.py
homeassistant/components/bluetooth/active_update_processor.py
```

参考 scanner registration、advertisement subscription、connectable/non-connectable、duplicate/update handling、matcher dispatch。

### Zeroconf / HomeKit / Matter discovery

```text
homeassistant/components/zeroconf/discovery.py
```

参考 service browser、service-info normalization、TXT/property matching、add/update/remove lifecycle。

### SSDP

```text
homeassistant/components/ssdp/scanner.py
```

参考 M-SEARCH、advertisement listener、ALIVE/BYEBYE/UPDATE、header matcher indexing。

### DHCP

```text
homeassistant/components/dhcp/__init__.py
```

参考 MAC/OUI/hostname matcher indexing、watcher lifecycle、address update dedupe。

B 的输出尽可能直接兼容 HA discovery/service-info，而不是自创 NearBy scanner schema。

---

## 14. B 线推进 gate

在 Agent D 至少验证以下三个原生入口前，不进入大规模 scanner 裁剪：

```text
Wi-Fi raw frame
 -> type / BSSID / RSSI / channel

BLE advertisement
 -> addr / RSSI / UUID / manufacturer data

802.15.4 frame
 -> length / RSSI / LQI / channel / MAC header
```

之后按顺序：

```text
B1 Wi-Fi management frame parser
B2 HA Bluetooth discovery adapter
B3 HA Zeroconf/SSDP/DHCP LAN discovery
B4 802.15.4 Zigbee/Thread candidate parser
```

---

## 15. License gate

技术可复用性和许可证可复制性分开记录。每个裁剪候选至少记录：

```text
source repo
source path
license
copy / adapt / reference-only
purpose
```

---

## 16. 安全边界

主线只做 passive observation、discovery、protocol identification、capture/analysis 和用户自有/授权设备交互。

明确排除 Wi-Fi deauth/WPA cracking/credential harvesting、BLE hijacking、Zigbee key extraction、TLS MITM 等攻击链。
