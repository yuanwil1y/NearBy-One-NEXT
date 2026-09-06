# Agent B 补充 — Home Assistant discovery normalization 参考

> 本文按 `main` 根 README 与本分支最新 `AGENTS.md` 重新收口。Home Assistant 是 B 的 discovery **字段/生命周期语义参考**，但 generated matcher tables、matcher indexing、integration/product recognition 已明确归 Agent C。旧版本中把 “HA matcher / matcher index” 当成 B runtime 职责的表述作废。

## 结论

B 的目标链路是：

```text
Agent D native observations
  -> Agent B protocol parse
  -> HA-compatible normalized discovery facts
  -> Agent C recognition lookup
  -> matched profile / parser_id
  -> optional compiled B parser
  -> Agent A semantic input
```

B 不生成最终产品 identity，不携带 HA generated matcher database，也不维护第二套 recognition table。

## Bluetooth

参考 HA Bluetooth matcher 使用的字段语义：

```text
local_name
service_uuid / service_uuids
service_data_uuid / service_data
manufacturer_id / manufacturer_data
manufacturer_data_start
connectable
```

ESP32-C6 输入必须直接来自 D 冻结的 NimBLE legacy / extended handoff。B 负责：

- AD structure bounds checking；
- 16/32/128-bit UUID normalization；
- service/manufacturer data bounded extraction；
- local-name / flags / TX power / appearance；
- 保留 RSSI/address/type/SID/PHY/data_status 等 native metadata；
- 将 `INCOMPLETE` / `TRUNCATED` 作为 incomplete source，不擅自拼接链式报告；
- 输出 typed matcher facts 给 C。

C 负责把这些 facts 对照 HA generated Bluetooth matcher 数据决定 `MATCHED / AMBIGUOUS / NOT_FOUND`。

## Zeroconf / mDNS

参考：

```text
homeassistant/components/zeroconf/discovery.py
homeassistant/generated/zeroconf.py
```

B 负责解析/规范化 DNS-SD discovery facts：service type、instance、host、port、TXT、A/AAAA，以及 HomeKit/Matter service/TXT 字段。C 拥有 generated Zeroconf/HomeKit matcher knowledge 与最终 lookup。

## SSDP

参考：

```text
homeassistant/components/ssdp/scanner.py
homeassistant/components/ssdp/common.py
homeassistant/generated/ssdp.py
```

B 只做 bounded HTTPU message parse + case-insensitive header normalization，并输出 ST/NT/USN/SERVER/LOCATION/MAN/MX 及可观察 manufacturer/model hints。C 做 matcher indexing / recognition。

## DHCP

参考：

```text
homeassistant/components/dhcp/
homeassistant/generated/dhcp.py
```

B 输出 message type、chaddr/MAC、hostname、vendor class、client identifier 等合法 discovery facts。OUI/hostname matcher tables 与匹配结果归 C。

## HomeKit / Matter

不另造 NearBy-specific scanner：

- HomeKit 主要复用 Zeroconf `_hap._tcp.local.` / `_hap._udp.local.` 发现事实；
- Matter 主要复用 `_matter._tcp.local.` / `_matterc._udp.local.` 与 BLE commissioning service data；
- VID/PID/discriminator 只有在协议中实际出现时才由 B 解析；
- 产品 identity 仍交 C。

## 当前优先级

1. BLE legacy + extended AD parser。
2. HA-compatible Bluetooth normalized facts -> C。
3. Zeroconf/mDNS。
4. SSDP。
5. DHCP。
6. complete-scan coordinator。
7. Wi-Fi management / 802.15.4 / Zigbee / Matter incremental parsing。

不要从 vendor-specific decoder 批量移植开始；先稳定 `native -> normalized facts -> C` 边界。
