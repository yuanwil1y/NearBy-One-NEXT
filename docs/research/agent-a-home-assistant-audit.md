# Agent A — Home Assistant 源码拆解与最大复用路线

> 目标：NearBy One NEXT 不重新设计设备模型。尽可能直接沿用 Home Assistant 的 discovery / integration / Device / Entity / State 逻辑与识别数据，只为 ESP32-C6 写必要的生成脚本和运行时胶水。
>
> 本轮审计基于 `home-assistant/core` 当前 `dev` 分支（2026-09-06）。Home Assistant Core 为 Apache-2.0；integration 的第三方 `requirements` 许可证必须分别核对。

## 0. 结论先行

NearBy One NEXT 不应尝试“移植 Home Assistant Core”。最佳路线是把 HA 拆成两部分：

1. **构建机上继续使用 Python/HA 上游数据**：读取 `manifest.json`、HA 生成逻辑和 integration metadata，生成 ESP32 可直接链接的只读表。
2. **ESP32 上只保留 HA 语义子集**：Device、Entity、State，以及各 integration 真正需要的设备 parser/codec。

运行时链路固定为：

```text
ESP-IDF scanner
  -> HA-compatible discovery record
  -> HA manifest matcher
  -> integration parser
  -> Device
  -> Entity
  -> State + attributes
  -> UI
```

不新增 Capability / ViewModel / DeviceGraph 等 NearBy 自定义抽象。

---

## 1. 第一优先级：直接拿 HA discovery 数据库

### 1.1 `homeassistant/loader.py`

重点：HA 对 integration manifest discovery 字段的正式 schema/TypedDict 定义。

需要镜像的 discovery 类型：

- `bluetooth`
- `zeroconf`
- `ssdp`
- `dhcp`
- `homekit`
- 后续按需扩展

Bluetooth matcher 当前核心字段包括：

- `local_name`
- `service_uuid`
- `service_data_uuid`
- `manufacturer_id`
- `manufacturer_data_start`
- `connectable`

**NearBy 策略：字段名和匹配语义保持 HA 原样。**

不要设计 `nearby_bt_match_t` 的另一套语义；C 结构体可以为了编译需要换语言表达，但字段含义和命名尽量一一对应。

### 1.2 `homeassistant/components/*/manifest.json`

这是 NearBy 最值钱的识别数据库之一。

当前实例：

#### Xiaomi BLE

`homeassistant/components/xiaomi_ble/manifest.json`

HA 当前直接以多个 Service Data UUID 匹配 Xiaomi BLE，并声明：

```text
requirements: xiaomi-ble==1.16.0
```

因此完整来源链是：

```text
BLE advertisement
 -> HA bluetooth matcher
 -> xiaomi_ble
 -> xiaomi-ble parser
 -> HA entities
```

NearBy 不应重新研究 FE95 等格式，优先追 `xiaomi-ble` 上游 parser。

#### SwitchBot

`homeassistant/components/switchbot/manifest.json`

当前 matcher 同时覆盖：

- service data UUID
- service UUID
- manufacturer ID
- connectable/non-connectable 条件

parser 依赖：

```text
PySwitchbot==2.7.0
```

这说明我们应该把 manifest 匹配规则和 parser 分成两个来源层：**识别抄 HA，解析继续追 requirement**。

#### Shelly

`homeassistant/components/shelly/manifest.json`

同一个 integration 同时声明：

- Bluetooth `local_name: Shelly*`
- Bluetooth manufacturer ID
- Zeroconf `_http._tcp.local.` + name matcher
- Zeroconf `_shelly._tcp.local.`

parser / protocol implementation 依赖：

```text
aioshelly==13.32.0
```

这个例子非常重要：**同一现实厂商可以通过多个 scanner 输入命中同一个 HA integration。** NearBy 不需要重新发明统一设备识别规则。

#### Matter

`homeassistant/components/matter/manifest.json`

当前 HA discovery 至少声明：

- `_matter._tcp.local.`
- `_matterc._udp.local.`

HA 本身依赖 Python Matter Server 客户端，不适合直接搬到 ESP32；但 Entity 映射和字段语义可作为参考。ESP32 侧实际协议实现优先用 Espressif Matter SDK。

---

## 2. 第二优先级：直接复用 HA 的生成思路，而不是运行时解析 JSON

### 2.1 `script/hassfest/zeroconf.py`

HA 自己已经在构建/校验阶段把 integration manifests 聚合成：

```text
homeassistant/generated/zeroconf.py
```

NearBy 应照这个方向做，而不是在 ESP32 上装 JSON parser 扫几千个 manifest。

推荐流程：

```text
pinned home-assistant/core source
        |
        | manifest.json / generated metadata
        v
host-side extractor
        |
        +--> ha_bluetooth_matchers.inc
        +--> ha_zeroconf_matchers.inc
        +--> ha_ssdp_matchers.inc
        +--> ha_dhcp_matchers.inc
        +--> ha_integration_index.inc
        v
ESP32 flash/rodata
```

### 2.2 新代码预算

这里允许写的“新代码”仅应是很薄的 host-side glue：

- 读取 HA manifest
- 保持 HA matcher 语义
- 字符串去重
- 转为 C/C++ 只读表
- 输出来源 integration domain

不要重新维护 NearBy 自己的 vendor/device database。

理想状态：以后只升级 `home-assistant/core` pin，再重新生成固件数据。

---

## 3. Device：抄字段和行为边界，不搬持久化系统

来源：

`homeassistant/helpers/device_registry.py`

HA `DeviceEntry` 当前核心描述字段包括：

- `identifiers`
- `connections`
- `manufacturer`
- `model`
- `model_id`
- name 相关字段
- configuration URL 等

NearBy 每次开机都是新环境，所以：

### 保留

- identifiers
- connections
- manufacturer
- model / model_id
- name
- 与 Entity 的关联

### 不需要搬

- 长期 registry storage
- migration
- orphan cleanup
- area/floor 长期归属
- persistent config entry 生命周期
- recorder/history 相关逻辑

**原则：删除持久化，不改变 Device 的产品语义。**

---

## 4. Entity：UI 的唯一统一能力入口

来源：

- `homeassistant/helpers/entity.py`
- `homeassistant/helpers/entity_registry.py`
- 各 `homeassistant/components/<domain>/`

HA Entity Registry 的正式字段里已经包含：

- `entity_id`
- `unique_id`
- `platform`
- `device_id`
- `domain`
- `device_class`
- `entity_category`
- name/icon 等
- supported features / capabilities 等描述字段

NearBy 不额外创造 Capability 层。

UI 应只理解 HA domain/entity semantics，例如：

```text
sensor.*
binary_sensor.*
switch.*
light.*
button.*
number.*
select.*
media_player.*
climate.*
cover.*
lock.*
```

v0.1 不需要一次实现全部 domain，但**新增类型时只增加 HA 已有 domain 的支持，不增加 NearBy 专有 domain**。

---

## 5. State：保留 HA 最核心三元组

目标运行时接口保持：

```text
entity_id
state
attributes
```

例如：

```text
sensor.xiaomi_temperature
state = 23.6
attributes.device_class = temperature
attributes.unit_of_measurement = °C
```

UI 不读取 BLE UUID / Zigbee Cluster / Matter Cluster / SSDP header。

它只看 domain、state、attributes。

NearBy 不需要搬：

- HA 全量 event bus
- recorder
- history
- statistics
- restore state
- template engine

每次启动创建空 state store，关机销毁。

---

## 6. Integration / Platform：最大复用的真正入口

不要把所有设备 parser 写进一个 `nearby_parser.c`。

目录和边界尽可能对齐 HA：

```text
integrations/
  xiaomi_ble/
  switchbot/
  shelly/
  matter/
  ...
```

每个 integration 负责：

1. discovery matcher 命中后的接管
2. 调用/承载该 integration 的 parser
3. 创建 Device
4. 创建 HA domain Entity
5. 更新 State
6. Entity service/action 映射到底层协议

不要另造 Provider/Adapter/Capability 命名体系。

---

## 7. HA 的“设备数据库”其实有三层，必须一起追

### Layer A — Manifest matcher 数据

可以高度自动化直接抽取。

典型：

- UUID
- local name
- manufacturer ID
- DHCP hostname/MAC 条件
- Zeroconf service/name/properties
- SSDP match fields

### Layer B — HA Integration Python

它决定：

- 使用哪个第三方 library
- 创建哪些 HA platforms/entities
- device info 如何映射
- 哪些状态是 primary/diagnostic

### Layer C — `requirements` 第三方 parser/library

大量真正的 vendor protocol knowledge 在这里。

已确认示例：

| HA integration | parser/library |
|---|---|
| `xiaomi_ble` | `xiaomi-ble==1.16.0` |
| `switchbot` | `PySwitchbot==2.7.0` |
| `shelly` | `aioshelly==13.32.0` |
| `matter` | `matter-python-client==1.4.0`, `matter-ble-proxy==0.7.1` |

因此后续自动审计工具应输出：

```text
domain
 -> discovery matchers
 -> requirements
 -> platforms
 -> source files
```

这将成为 NearBy 的“上游搬运清单”。

---

## 8. 复用分级

### A — 优先直接复制/生成

- manifest discovery rules
- HA domain names
- device class names
- entity/state attribute naming
- Device/Entity 字段语义
- generated recognition maps
- 小型纯数据 tables/constants（逐文件核许可证）

### B — 轻改后使用

- advertisement parser 中纯 bytes/TLV decode 部分
- integration 的 device/entity mapping
- protocol-independent transform/enum tables
- HA hassfest 的生成思路/部分 host-side code

### C — 只借鉴，不移植

- asyncio runtime
- config flow UI
- config entries 持久化
- HA event bus 全量实现
- storage registry
- recorder/history/statistics
- HTTP/websocket frontend backend
- Python Matter Server 架构

---

## 9. 第一条纵向链路应该怎么抄

第一条建议：`BLE -> HA matcher -> Xiaomi BLE -> sensor entities -> LVGL`。

步骤：

```text
ESP-IDF NimBLE scan
  -> HA Bluetooth discovery record
  -> generated HA matcher table
  -> domain=xiaomi_ble
  -> xiaomi-ble parser 的最小 decode 部分移植/复用
  -> DeviceInfo-equivalent data
  -> sensor.temperature
  -> sensor.humidity
  -> sensor.battery
  -> State
  -> UI
```

为什么先选它：

- discovery 完全被动
- 不需要连接
- manifest 规则简单
- Entity 类型简单
- 很适合验证“HA 数据库不是文档，而是真正在 MCU 上驱动 UI”的路线

第二条建议：SwitchBot BLE。

第三条建议：Shelly BLE + Zeroconf，验证两个 scanner 输入命中同一 integration 的能力。

---

## 10. 建议的仓库结构（保持上游感，而不是发明框架）

```text
components/
  ha_core/
    device/
    entity/
    state/
    discovery/

  integrations/
    xiaomi_ble/
    switchbot/
    shelly/

  scanners/
    bluetooth/
    wifi/
    zeroconf/
    ssdp/
    dhcp/

tools/
  ha_extract/

generated/
  ha/
    bluetooth_matchers.inc
    zeroconf_matchers.inc
    ssdp_matchers.inc
    dhcp_matchers.inc
    integration_index.inc
```

名字可以继续收敛，但不要添加新的领域抽象。

---

## 11. License gate

Home Assistant Core 本身是 Apache-2.0，直接复制修改允许范围较宽，但必须保留要求的版权/NOTICE/许可证信息。

**关键风险不是 HA Core，而是 integration requirements。**

后续每个 parser port 都必须记录：

```text
source repository
source commit/tag
source file
license
copied/modified/generated/reference-only
```

推荐仓库后续增加 `THIRD_PARTY.md`，所有搬运代码都登记来源。

---

## 12. Agent A 后续执行清单

- [ ] 写 host-side HA manifest extractor（只做 glue，不设计新 DB）
- [ ] 扫描全部 integrations，生成 discovery coverage 统计
- [ ] 输出 `domain -> matcher -> requirement -> platform` 索引
- [ ] 统计 BLE matcher 总量及可在 ESP32 被动扫描覆盖的 integration 数
- [ ] 统计 Zeroconf / SSDP / DHCP 可覆盖 integration 数
- [ ] 选择前 20 个 BLE parser，逐个追第三方源码和许可证
- [ ] 先移植 `xiaomi_ble` 最小纵向链路
- [ ] 再移植 `switchbot`
- [ ] 用 `shelly` 验证 BLE + LAN discovery
- [ ] 定义最小 HA Device / Entity / State C structs（字段沿用 HA，不创造新概念）
- [ ] 添加 upstream provenance / license 自动清单

## Source map

- https://github.com/home-assistant/core
- `homeassistant/loader.py`
- `homeassistant/helpers/device_registry.py`
- `homeassistant/helpers/entity_registry.py`
- `homeassistant/helpers/entity.py`
- `homeassistant/core.py`
- `homeassistant/components/bluetooth/match.py`
- `script/hassfest/zeroconf.py`
- `homeassistant/generated/zeroconf.py`
- `homeassistant/components/*/manifest.json`

### 已抽查 manifests

- `homeassistant/components/xiaomi_ble/manifest.json`
- `homeassistant/components/switchbot/manifest.json`
- `homeassistant/components/shelly/manifest.json`
- `homeassistant/components/matter/manifest.json`
