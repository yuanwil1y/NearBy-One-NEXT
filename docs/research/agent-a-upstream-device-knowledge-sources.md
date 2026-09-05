# Agent A 补充 — Home Assistant 生态设备知识库来源

> 目标：确认 Home Assistant 及其生态中除了 `manifest.json` / generated matcher 之外，还有哪些成熟“设备数据库/设备知识库”可以直接抽取到 NearBy One NEXT 的 SD Recognition Database。

## 0. 结论

**有，而且不止一个。** Home Assistant 自身没有一个统一的 `devices.db`，设备知识散布在多个上游项目中。对 NearBy One NEXT 最重要的来源应分层处理：

1. **HA Core discovery matcher**：负责“这个广播/服务属于哪个 integration”。
2. **ZHA quirks / zha-device-handlers**：负责 Zigbee 设备的 manufacturer/model/signature、endpoint/cluster 修正、实体暴露规则，是非常接近真正设备数据库的来源。
3. **Zigbee2MQTT / zigbee-herdsman-converters**：另一套规模很大的 Zigbee 型号/能力/converter 数据库，MIT，适合和 ZHA quirks 交叉补全。
4. **ESPHome Devices**：真实商品/模块的 manufacturer/model/configuration catalog，适合做产品型号、别名、板型和元数据补全，但它不是无线指纹数据库。
5. **HA integration requirements**：Xiaomi BLE、SwitchBot、Shelly 等 parser 包里的产品表和 decoder，是 BLE/Wi-Fi 私有协议识别的重要来源。
6. **标准数据库**：IEEE OUI、Bluetooth SIG Assigned Numbers、Matter VID/PID、Zigbee manufacturer/cluster IDs。

因此 SD 数据库不应只叫“HA database”，而应是一个 **多上游编译生成的 Recognition Database**，HA 的 Device/Entity 语义仍然作为最终输出标准。

---

## 1. Home Assistant Core：Discovery / Matcher 数据库

继续作为第一入口：

```text
homeassistant/components/*/manifest.json
homeassistant/generated/*
homeassistant/loader.py
script/hassfest/*
```

主要可抽取：

- Bluetooth: local_name / service_uuid / service_data_uuid / manufacturer_id / manufacturer_data prefix
- Zeroconf: service type / instance name / TXT properties
- HomeKit model matcher
- SSDP header matcher
- DHCP hostname / MAC matcher
- USB VID/PID（对 NearBy 主目标价值较低）

这层最适合回答：

```text
observation -> integration id
```

但通常不能单独回答：

```text
integration id -> 具体型号的完整 Entity / 私有协议解析
```

---

## 2. ZHA quirks：优先级极高

Repository: `zigpy/zha-device-handlers`

Home Assistant 当前 ZHA manifest 直接依赖 `zha-quirks`；上游 repo 就是 `zha-device-handlers`。

许可证：Apache-2.0。

### 为什么它非常适合 NearBy

ZHA quirks 本质是在描述：

```text
manufacturer + model + Zigbee signature
    -> 正确 endpoint / cluster 结构
    -> 替换/修复非标准 cluster
    -> 应暴露的 Entity
```

V1 quirks 常见结构：

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

V2 `QuirkBuilder` 更接近声明式设备数据库：

```text
QuirkBuilder("SONOFF", "SNZB-04P")
  .replaces(...)
  .binary_sensor(...)
```

还有：

- `.applies_to(manufacturer, model)`
- firmware version filter
- enum / number / sensor / binary_sensor 等 Entity 描述
- endpoint / cluster replacement
- default Entity 抑制

### NearBy 数据库化建议

把 quirks 分两类：

#### DATA_ONLY

可以直接转 SD：

- manufacturer/model aliases
- signature endpoint list
- input/output cluster IDs
- device type/profile id
- firmware filters
- exposed entity metadata
- simple attribute mapping

#### DATA + CODE

需要固件 parser/cluster 实现：

- custom cluster class
- Tuya datapoint conversion
- manufacturer-specific command encoding
- 特殊 attribute transform

第一阶段先把 ZHA quirks 当作 Zigbee Recognition DB 的第一主源。

---

## 3. Zigbee2MQTT / zigbee-herdsman-converters：强烈建议加入

Repository: `Koenkk/zigbee-herdsman-converters`

许可证：MIT。

它本身就是“Collection of device converters”，拥有大量：

- zigbeeModel
- fingerprint
- manufacturer/model/vendor/description
- exposes
- endpoint mapping
- fromZigbee / toZigbee converter
- configure logic

它和 ZHA quirks 的覆盖并不完全重合。

### 建议用途

```text
ZHA quirks
      +
zigbee-herdsman-converters
      ↓
统一 Zigbee product index
```

若两边都支持同一设备：

- manufacturer/model/fingerprint 用来交叉验证
- Entity 语义最终仍映射到 HA domain/device_class
- converter 代码按许可证和 MCU 复杂度决定是否移植

由于其 MIT 许可证，技术上比很多 GPL 参考库更适合作为直接转换/裁剪来源。

---

## 4. ESPHome Devices：有数据库，但用途不同

Repository: `esphome/devices.esphome.io`

README 明确定义它是：

> a database of configuration files and guides for devices commonly flashed with ESPHome firmware

设备页位于：

```text
src/docs/devices/
```

当前 schema 已包含：

- manufacturer
- model
- type
- board
- standard
- aliases
- description

大量页面还包含完整 ESPHome YAML，里面进一步记录：

- GPIO / relay / LED / button 映射
- sensor platform
- chipset/board
- 商品型号
- device_make / device_model

### 对 NearBy 有什么价值

**有价值，但不是第一识别源。**

它比较适合：

```text
已经知道 manufacturer/model
        ↓
补充标准商品名 / alias / device type / metadata
```

而不是：

```text
随机 BLE/Wi-Fi 广播
        ↓
直接识别成 ESPHome Devices 某型号
```

原因是设备刷入 ESPHome 后，其无线身份通常由用户配置决定，并不一定保留原商品的可唯一匹配 fingerprint。

### 许可证注意

`devices.esphome.io` 当前仓库为 GPLv3。若直接把其内容转成随固件/数据库分发的数据，需要单独决定许可证策略。第一阶段可以作为参考/补全源，不应在许可证未定前直接批量导入最终数据库。

---

## 5. ESPHome Core 本身的价值

Repository: `esphome/esphome`

它更适合当：

- `_esphomelib._tcp` / native API 行为参考
- ESP32 组件/驱动代码参考
- sensor/switch/light 等实体语义参考
- BLE proxy / scanner implementation 参考

它不是一个大型商品 fingerprint DB。

NearBy 扫到 ESPHome 设备时，最可靠的路径通常是：

```text
mDNS `_esphomelib._tcp`
  -> ESPHome integration
  -> native API DeviceInfo
  -> entities
```

也就是说，对 ESPHome 设备而言“在线询问设备自身”往往比查静态数据库更准确。

---

## 6. HA 第三方 parser 包：BLE/Wi-Fi 私有协议知识的核心来源

很多 HA integration 的真正型号数据库并不在 HA Core，而在 manifest `requirements`。

例如：

```text
xiaomi_ble -> xiaomi-ble
switchbot  -> PySwitchbot
shelly     -> aioshelly
```

这些包经常包含：

- product id -> model
- manufacturer data layouts
- sensor mappings
- encryption/CRC/version rules
- supported device sets

Agent A 应继续自动追踪：

```text
HA manifest
  -> requirements
  -> upstream repo
  -> license
  -> static model tables
  -> decoder candidates
```

这部分对 BLE 识别尤其重要。

---

## 7. 另一个值得评估的 BLE 数据源：Theengs Decoder

Repository: `theengs/decoder`

特点：

- C 为主
- 明确面向 ESP32 / IoT payload decoding
- 内置大量 BLE/IoT device decoder

技术上非常符合 NearBy 的目标，但当前许可证为 GPLv3。

建议：

- Agent A/B 都列为高价值 reference source
- 若 NearBy 最终采用兼容 GPLv3 的许可证，可重新评估直接复用
- 若不采用 GPLv3，则只参考协议行为，不直接复制代码/数据库

---

## 8. 推荐的 SD 数据库来源优先级

### Tier A — 优先直接生成数据库

1. HA Core manifests/generated — Apache-2.0
2. ZHA quirks / zha-device-handlers — Apache-2.0
3. zigbee-herdsman-converters — MIT
4. 标准 assigned-number/OUI 数据（逐项确认条款）
5. HA requirements 中许可友好的静态 model tables

### Tier B — 参考/需许可证决策

6. ESPHome Devices — GPLv3
7. Theengs Decoder — GPLv3
8. GPL Wireshark/Aircrack/Bettercap 等数据库/代码

---

## 9. 建议生成后的数据库结构

```text
/nearby/db/
  ha_discovery.bin

  bluetooth/
    matchers.bin
    products.bin
    assigned_numbers.bin

  zigbee/
    products.bin
    signatures.bin
    quirks.bin
    entities.bin
    converters.bin   # 只存声明式映射，不存任意代码

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
    strings.bin
    sources.bin
    licenses.bin
```

每条 record 保存 source id，使同一设备可以同时拥有：

```text
source=HA
source=ZHA
source=Zigbee2MQTT
source=ESPHomeDevices
```

并在构建时决定冲突优先级，而不是 ESP32 运行时临时猜。

---

## 10. 当前最重要的新结论

之前把数据库理解成“HA matcher + OUI”还太窄。

更准确的目标应该是：

```text
HA Discovery Knowledge
      +
ZHA Quirk Knowledge
      +
Zigbee2MQTT Device Knowledge
      +
HA Integration Parser Product Tables
      +
ESPHome Product Catalog Metadata
      +
Standards Assigned Numbers
      ↓
NearBy Recognition Database on SD
```

其中 ZHA quirks 是目前发现最适合直接转换为 NearBy Zigbee 数据库的来源之一，应该提升到 Agent A 第一优先级。