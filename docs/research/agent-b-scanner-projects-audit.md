# Agent B — 扫描/协议 parser 参考项目拆解（收口版）

> 当前 canonical spec 是 `main` 根 README；执行边界由本分支最新 `AGENTS.md` 细化。本文仅保留 parser/scanner 参考价值。旧版本里将 fingerprint/product matching、HA matcher dispatch/indexing 归入 B 的描述已废弃；这些属于 Agent C。

## 1. D -> B -> C 边界

```text
Agent D
ESP-IDF / NimBLE / 802.15.4 native input + driver lifecycle
        ↓
Agent B
scan orchestration + protocol parsing + normalized facts
        ↓
Agent C
recognition DB / matcher knowledge / product identity
        ↓
Agent A
HA Device / Entity / State
```

B 不发明 universal packet schema，不重写 D driver，不维护 product table，也不从 manufacturer/OUI/VID/PID 直接宣布产品 identity。

D 冻结基线：`accdf91441d0abaf6a5d69aac31394465ab49b06`。

## 2. 参考项目定位

| 项目 | B 可用部分 | 策略 |
|---|---|---|
| Home Assistant Core | Bluetooth/Zeroconf/SSDP/DHCP discovery 字段与生命周期语义 | 优先参考/窄移植 |
| ESP-IDF / ESP-NimBLE / lwIP | native structs、driver/network APIs、协议组件 | 直接依赖官方接口 |
| hcxdumptool | 802.11 management/IE parsing 参考 | 仅被动、安全子集 |
| Wireshark | dissector 字段真值、异常包行为 | reference-only |
| Scapy | PC 测试向量 / packet schema | test/reference |
| Kismet | RF inventory / 802.11 field semantics | reference-only |
| Aircrack-ng | airodump management frame parsing 参考 | reference-only/窄移植评估 |
| BtleJack / nRF Sniffer | BLE packet/scan 行为参考 | reference-only unless license/code path cleared |
| Bettercap | scanner lifecycle/channel scheduling 概念 | reference-only |
| Responder | LAN discovery packet identification | 仅解析参考；poisoning 全排除 |
| mitmproxy | HTTP message model | 低优先级 reference-only；不做 TLS MITM |

## 3. 当前实现顺序

1. 消费 D frozen Wi-Fi/NimBLE/802.15.4 public contracts。
2. 清掉 B-owning-recognition 的旧假设。
3. BLE legacy + extended AD parser + host vectors。
4. HA-compatible Bluetooth normalized facts / matcher inputs 给 C。
5. Zeroconf/mDNS、SSDP、DHCP。
6. 完整 scan coordinator：真实 module terminal progress + D gate semantics。
7. Wi-Fi management，再到 802.15.4/Zigbee/Matter。
8. host corruption/fuzz + ESP-IDF v6.1 CI。

## 4. BLE

直接消费 D 的：

```text
nearby_ble_disc_record_t
nearby_ble_ext_disc_record_t
```

B 解析 standard AD structures 并输出：

```text
local_name
service UUIDs
service data UUID + bounded bytes
manufacturer company ID + bounded bytes
flags / TX power / appearance
connectable when native metadata can prove it
RSSI / address type / SID / PHY / data_status
```

Extended `INCOMPLETE/TRUNCATED` 不在 D 层重组，B 也不能把未完整报告假装成 complete observation。

## 5. LAN discovery

### Zeroconf / mDNS

只解析 discovery 所需 DNS header/name compression/PTR/SRV/TXT/A/AAAA。严格 bounds、compression pointer loop limit、name/TXT caps。B 输出 service/TXT facts，C 识别。

### SSDP

只做 bounded HTTPU parser，header 名 case-insensitive，保留 ST/NT/USN/SERVER/LOCATION/MAN/MX 等 matcher facts。B 不持有 HA generated SSDP matcher table。

### DHCP

只提取合法 discovery 字段：message type、chaddr、hostname、vendor class、client identifier。无 rogue DHCP、无 credential path。

## 6. Wi-Fi / 802.15.4

Wi-Fi 只做 passive management frame/beacon/probe 信息与 bounded IE parse；不做 deauth/injection/handshake capture/cracking。

802.15.4 先做 bounded MAC header/payload boundary，再逐步加 Zigbee discovery facts；不做 key extraction 或未授权网络加入。

## 7. 安全与许可证

所有复制/适配代码必须记录 repo、immutable revision、source path、license 和 copied/modified/translated/reference-only 分类。GPL/不明确来源默认 reference-only，除非项目整体分发策略明确允许。

主线仅允许 passive observation、discovery、协议解析和用户自有/授权设备交互。明确排除 deauth、credential capture/cracking、poisoning、rogue auth、BLE hijacking、Zigbee key extraction、TLS MITM。
