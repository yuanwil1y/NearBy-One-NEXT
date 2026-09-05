# Agent B 补充 — Home Assistant 原生扫描 / 发现模块

> Home Assistant 不只作为识别数据库和 Entity 模型来源，也作为 NearBy One NEXT discovery/scanner 层的一等参考源。重点复用其 Bluetooth、Zeroconf/mDNS、SSDP/UPnP、DHCP/LAN、HomeKit 和 Matter discovery 的工程化组织方式；ESP32-C6 只替换 Linux/Python/DBus/socket 等不可运行的底层 I/O。

## 结论

NearBy 的 scanner 层应优先输出 **HA-compatible discovery/service-info**，不要再发明独立 `ScannerRecord`：

```text
ESP-IDF scanner
  -> HA-like ServiceInfo / Advertisement event
  -> HA matcher
  -> HA integration/parser
  -> Device / Entity / State
```

HA 与其他参考项目的职责不同：Wireshark 是协议字段真值；hcxdumptool/Kismet/airodump 偏 raw 802.11 与 inventory；BtleJack/nRF Sniffer 偏 BLE 链路观察；**HA 强在 scanner orchestration + cache + matcher + discovery lifecycle + integration dispatch**。

## 1. Bluetooth

重点源码：

```text
homeassistant/components/bluetooth/
  manager.py
  api.py
  match.py
  models.py
  passive_update_processor.py
  active_update_processor.py
  manifest.json
```

值得复刻：

- advertisement/service info 数据边界
- scanner registration / callback 模型
- connectable 与 passive scanner 区分
- discovered-service cache
- matcher 调度
- passive advertisement 持续更新 integration/entity 的模式

ESP32 替换：

```text
Bleak / DBus / Linux adapter -> NimBLE / ESP-IDF GAP callbacks
```

HA Bluetooth matcher 字段直接沿用：

```text
local_name
service_uuid
service_data_uuid
manufacturer_id
manufacturer_data_start
connectable
```

优先级：**A**。

## 2. Zeroconf / mDNS / DNS-SD

重点源码：

```text
homeassistant/components/zeroconf/discovery.py
homeassistant/generated/zeroconf.py
script/hassfest/zeroconf.py
```

`ZeroconfDiscovery` 已覆盖：

- service type browser
- add/update/remove 生命周期
- TXT properties
- IPv4/IPv6 地址选择
- wildcard/fnmatch matcher
- HomeKit `_hap._tcp.local.` / `_hap._udp.local.`
- HomeKit model lookup
- discovery -> integration dispatch

NearBy 路径：

```text
ESP-IDF mDNS result
  -> HA-like ZeroconfServiceInfo
  -> HA generated matcher table
  -> integration
```

优先级：**A**。

## 3. SSDP / UPnP

重点源码：

```text
homeassistant/components/ssdp/scanner.py
homeassistant/components/ssdp/server.py
homeassistant/components/ssdp/common.py
```

HA 当前 SSDP Scanner 已有：

- multicast + broadcast scan
- advertisements
- ALIVE / BYEBYE / UPDATE
- device tracker
- description cache
- callback registration
- integration matcher
- case-insensitive headers
- IPv4/IPv6 source handling

`IntegrationMatchers` 会对主要字段预索引，而不是每包遍历完整数据库。主要字段包括 manufacturer、ST、UPnP device type、NT、manufacturer URL。

ESP32 路径：

```text
lwIP UDP/1900
  -> SSDP headers
  -> HA-like SsdpServiceInfo
  -> HA IntegrationMatchers
  -> integration
```

优先级：**A/B**。

## 4. DHCP / LAN host discovery

重点源码：

```text
homeassistant/components/dhcp/__init__.py
homeassistant/components/dhcp/helpers.py
homeassistant/components/dhcp/models.py
```

HA DHCP discovery 实际组合多类 watcher，包括 DeviceTrackerWatcher、NetworkWatcher、DHCPWatcher、RediscoveryWatcher 等。

最值得 NearBy 直接复刻的是 matcher indexing：

1. registered devices
2. 无 OUI 的 hostname matcher
3. 按 MAC OUI 分桶的 matcher

NearBy 可将 DHCP、ARP/neighbour、mDNS/SSDP 补充得到的 MAC/IP/hostname 都喂进同一套 HA-compatible matcher。

优先级：matcher/index **A**；HA 的具体 watcher runtime **B/C**。

## 5. HomeKit / Matter

不要另造 scanner。

HomeKit 已由 HA Zeroconf 处理：

```text
_hap._tcp.local.
_hap._udp.local.
```

Matter integration 的 discovery 也走 Zeroconf service：

```text
_matter._tcp.local.
_matterc._udp.local.
```

NearBy 应使用 HA discovery 语义 + ESP-Matter 原生 API，而不是再写 NearBy 专属 Matter/HomeKit 扫描层。

## 6. B 线更新后的优先级

| 来源 | NearBy 中的角色 | 优先级 |
|---|---|---|
| HA Bluetooth | BLE discovery orchestration / matcher / passive update | **A** |
| HA Zeroconf | mDNS/DNS-SD/HomeKit/Matter discovery | **A** |
| HA SSDP | UPnP discovery / lifecycle / matcher index | **A/B** |
| HA DHCP | LAN host aggregation / OUI+hostname matcher index | **A/B** |
| hcxdumptool | raw 802.11 passive scan / IE parsing | **A** |
| BtleJack | BLE packet/state reference | A/B |
| Wireshark | protocol parser oracle | reference |
| Kismet | Wi-Fi inventory/fingerprinting | reference |
| airodump-ng | AP/station inventory | B/C |
| Bettercap | recon lifecycle concepts | C |
| Scapy | packet schema + PC test vectors | test/reference |
| nRF Sniffer | BLE scanner behavior | reference |
| Responder | LAN protocol identification only | C |

## 7. 新原则

在写任何新的 scanner glue 前，先检查 HA 是否已经定义：

- ServiceInfo / Advertisement 数据形状
- matcher
- cache / dedup
- add/update/remove 生命周期
- scanner callback
- integration dispatch

如果 HA 已经有，就优先复刻其行为和字段；仅把底层输入替换为 ESP-IDF。
