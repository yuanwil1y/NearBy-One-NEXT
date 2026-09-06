# Agent C — NearBy One NEXT 设备识别数据库与生态知识库

> 目标：专门负责 NearBy One NEXT 的大型设备识别数据库。把 Home Assistant 及其生态、ZHA quirks、Zigbee2MQTT、ESPHome Devices、标准 assigned numbers、许可友好的 vendor parser tables 等上游知识，尽量以“构建期自动生成、运行时只读、SD 卡按需查询”的方式复用。Agent C 不负责 HA Device/Entity 运行时语义，也不负责底层 scanner。

## 0. Agent C 边界

Agent C 负责：

- 设备识别数据源盘点与许可证审计
- HA discovery matcher 抽取
- ZHA quirks / zha-device-handlers 抽取
- zigbee-herdsman-converters 抽取
- ESPHome Devices 商品型号/alias/元数据抽取
- HA integration requirements 中静态 product/model tables 抽取
- IEEE OUI / Bluetooth SIG / Matter / Zigbee assigned-number 数据整理
- `DATA_ONLY / DATA+PARSER / CODE_ONLY` 分类
- SD Recognition Database 格式设计
- PC 端 generator / index builder / license report 方案
- 数据库大小、查找速度、RAM 使用和 coverage benchmark

Agent C 不负责：

- Home Assistant `Device / Entity / State / Registry` 运行时实现（Agent A）
- Wi-Fi/BLE/LAN/802.15.4 scanner 与 packet parser（Agent B）
- 新建自定义 Capability/ViewModel 等语义层

最终接口应保持简单：

```text
scanner observation
  -> recognition lookup
  -> integration/profile/model metadata
  -> Agent A 的 HA-compatible Device/Entity pipeline
```

---

## 1. 核心结论

NearBy 每次开机仍是全新 Session；运行时 Device/Entity/State 只放 RAM。

SD 卡承载的是只读 **Recognition Database**，不是设备历史数据库。

建议目标结构：

```text
/nearby/db/
  index.bin
  strings.bin

  bluetooth/
    matchers.bin
    products.bin
    assigned_numbers.bin

  zigbee/
    products.bin
    signatures.bin
    quirks.bin
    entities.bin
    converters.bin

  lan/
    zeroconf.bin
    ssdp.bin
    dhcp.bin

  matter/
    vendors.bin
    products.bin

  vendors/
    oui.bin
    aliases.bin

  metadata/
    sources.bin
    licenses.bin
    build-info.bin
```

1GB SD 对这类只读识别数据非常充裕；真正需要控制的是 ESP32 的 RAM、随机读取次数和固件中 parser 的代码体积。

---

## 2. 第一数据源：Home Assistant Core

重点抽取：

```text
homeassistant/components/*/manifest.json
homeassistant/generated/*
homeassistant/loader.py
script/hassfest/*
```

用于：

- Bluetooth `local_name / service_uuid / service_data_uuid / manufacturer_id / manufacturer_data prefix`
- Zeroconf service type / instance / TXT matcher
- HomeKit model matcher
- SSDP header matcher
- DHCP hostname / MAC/OUI matcher
- Matter/ZHA 等 integration discovery metadata

这层主要回答：

```text
observation -> integration id
```

不要把 HA Core 的 Python runtime 搬进 ESP32；优先复用 HA 自己的 generated/hassfest 思路，在 PC 构建机上生成 NearBy 数据文件。

---

## 3. 第二数据源：ZHA quirks / zha-device-handlers

Repository: `zigpy/zha-device-handlers`
License: Apache-2.0

这是 Agent C 的第一优先级之一。

ZHA quirks 本质描述：

```text
manufacturer + model + Zigbee signature
  -> endpoint/cluster 修正
  -> custom cluster replacement
  -> 应暴露的 Entity
```

V1 常见信息：

```text
signature
  MODELS_INFO
  ENDPOINTS
  INPUT_CLUSTERS
  OUTPUT_CLUSTERS
replacement
  ENDPOINTS
  custom clusters
```

V2 `QuirkBuilder` 更适合自动抽取：

```text
QuirkBuilder("SONOFF", "SNZB-04P")
  .applies_to(...)
  .replaces(...)
  .binary_sensor(...)
  .sensor(...)
  .number(...)
```

Agent C 应尝试把声明式部分转换成 SD 记录：

- manufacturer/model aliases
- endpoint/cluster signature
- device type/profile id
- firmware filter
- exposed entity metadata
- simple attribute mapping
- default entity suppression

custom cluster class、Tuya datapoint transform、特殊 command codec 等归入 `DATA+PARSER`，不强行数据库化。

---

## 4. 第三数据源：Zigbee2MQTT converters

Repository: `Koenkk/zigbee-herdsman-converters`
License: MIT

重点：

- `zigbeeModel`
- fingerprint
- manufacturer/model/vendor/description
- exposes
- endpoint mapping
- fromZigbee / toZigbee converter metadata
- configure metadata

建议和 ZHA quirks 合并生成统一 Zigbee product index：

```text
ZHA quirks
   +
zigbee-herdsman-converters
   -> normalized Zigbee records
```

同一设备多来源时保留 `source_id`，构建期做冲突决策，ESP32 runtime 不临时猜。

---

## 5. ESPHome Devices

Repository: `esphome/devices.esphome.io`

它是大型商品/配置 catalog，不是无线 fingerprint DB。

适合抽取：

- manufacturer
- model
- type
- aliases
- board / chipset
- standard
- description
- 商品元数据

推荐用途：

```text
已经识别出 manufacturer/model
  -> ESPHome Devices 补充标准名称/alias/type/metadata
```

不建议把它作为随机 BLE/Wi-Fi 广播的第一识别入口。

许可证为 GPLv3，批量转换后随产品分发前必须明确许可策略；未决定前标为 reference/enrichment source。

---

## 6. HA integration requirements / vendor parser tables

很多 BLE/Wi-Fi 私有设备知识不在 HA Core，而在 integration requirements，例如：

```text
xiaomi_ble -> xiaomi-ble
switchbot  -> PySwitchbot
shelly     -> aioshelly
```

Agent C 要自动追：

```text
HA manifest
 -> requirements
 -> upstream repo
 -> license
 -> static model/product tables
 -> decoder dependency
```

对每个 integration 生成记录：

```text
integration
  discovery types
  matcher count
  upstream parser
  parser language
  parser license
  static table size
  estimated parser code size
  DATA_ONLY / DATA+PARSER / CODE_ONLY
```

这是最终覆盖率评估的核心表。

---

## 7. 标准数据库

逐项确认许可证/使用条款后，纳入：

- IEEE OUI
- Bluetooth SIG Company Identifiers
- BLE assigned UUIDs / Appearance 等 Assigned Numbers
- Matter Vendor ID / Product metadata
- Zigbee manufacturer IDs / cluster metadata

这些数据非常适合只读 sorted index / prefix index。

---

## 8. Theengs 等额外来源

`theengs/decoder` 技术上很适合 ESP32 和 BLE payload decoding，但当前 GPLv3。

Agent C 的处理：

- 可做协议覆盖率参考
- 可做测试 oracle/行为参考
- 只有在项目许可证策略允许时，才考虑直接复制数据库/代码

不要因为“代码是 C”就忽略许可证边界。

---

## 9. 数据 vs 代码分类

### DATA_ONLY

优先全部放 SD：

- matcher
- OUI
- UUID/company IDs
- model aliases
- VID/PID
- Zigbee signature
- mDNS/SSDP/DHCP matcher
- simple entity metadata
- static product tables

### DATA+PARSER

SD 放识别信息，固件保留小 parser：

- Xiaomi BLE payload
- SwitchBot proprietary packet
- vendor bitfield/CRC/AES decode
- Zigbee custom cluster transforms

### CODE_ONLY

不值得强行做数据库：

- 大型状态机
- 复杂连接/控制协议
- 动态 Python/JS plugin 行为

Agent C 只负责标记，真正 parser 移植归对应 integration/scanner 实现任务。

---

## 10. 数据库格式 benchmark

必须实际比较：

### A. SQLite read-only

优点：成熟、索引方便、PC 生成简单。
缺点：ESP32 Flash/RAM/page cache/随机 IO 成本可能过高。

### B. CBOR/MessagePack 分片

优点：简单、紧凑。
缺点：需要 decode，索引能力一般。

### C. Flat binary + sorted indexes

当前首选候选。

```text
manufacturer_id -> bucket/binary search
service_uuid    -> sorted index
OUI             -> prefix table
mDNS type       -> bucket
Zigbee model    -> hash/sorted string id
```

只在 RAM 放顶级索引/热点 cache，具体 record 从 SD 按需读取。

---

## 11. 构建工具目标

建议未来产出：

```text
tools/dbgen/
  extract_ha.py
  extract_zha_quirks.py
  extract_z2m.py
  extract_esphome_devices.py
  extract_requirements.py
  normalize.py
  build_bluetooth.py
  build_zigbee.py
  build_lan.py
  build_matter.py
  build_oui.py
  build_strings.py
  license_report.py
  coverage_report.py
```

原则：优先调用/复用上游已有 generator/schema，不重复写 parser。

生成物必须记录：

- schema version
- upstream commit/tag
- source id
- license id
- build timestamp
- CRC/hash
- required parser ids

这样 DB 和 firmware 可以做兼容性检查。

---

## 12. 第一阶段 PoC

不要一上来做全库。

先做：

```text
nearby-db-v0/
  bluetooth.bin   # HA bluetooth matcher
  zeroconf.bin    # HA zeroconf/HomeKit matcher
  oui.bin
  strings.bin
```

验证：

```text
NimBLE advertisement
 -> SD lookup
 -> integration id
 -> firmware parser if present
 -> HA-compatible Device/Entity
```

第二阶段加入：

```text
zigbee.bin
  <- ZHA quirks + zigbee-herdsman-converters
```

并统计：

- 数据库大小
- 冷启动时间
- 单次 lookup latency
- RAM peak
- SD IO 次数
- matcher 命中率
- 有 parser / 仅识别 / 无支持的设备比例

---

## 13. Agent C 的交付物

Agent C 最终至少要交付：

1. 上游设备知识源清单与许可证矩阵
2. 每个源可直接复制/可转换/只能参考的分级
3. `DATA_ONLY / DATA+PARSER / CODE_ONLY` integration coverage 表
4. SD 数据库 schema v0
5. SQLite vs CBOR vs flat-binary benchmark
6. HA Bluetooth/Zeroconf/OUI PoC 生成器
7. ZHA quirks / Zigbee2MQTT 提取 PoC
8. 数据库 build manifest + license report
9. 1GB SD 和 ESP32-C6 约束下的容量/性能结论

核心原则不变：**能从成熟上游生成，就不手写；能复制声明式数据，就不重新维护一份 NearBy 私有设备库。**
