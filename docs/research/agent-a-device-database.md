# Agent A 补充 — NearBy One NEXT 设备识别数据库方案

> 目标：评估是否应把 Home Assistant 及其生态中的庞大识别数据放到 1GB SD 卡，并设计一个“构建期自动生成、运行时只读、按需查询”的设备数据库。核心原则仍然是最大化复用上游数据，尽量不写新的设备识别逻辑。

## 0. 结论

**应该做设备数据库，但不要把它理解成传统持久化业务数据库。**

NearBy 每次开机都是新 Session，因此运行时 Device/Entity/State 仍然只放 RAM；SD 卡主要承载一个大型、只读的 **Recognition Database**：

```text
SD Card
  /nearby/db/
      index.bin
      bluetooth.bin
      zeroconf.bin
      ssdp.bin
      dhcp.bin
      oui.bin
      matter.bin
      zigbee.bin
      vendor_models.bin
      strings.bin
      licenses/
```

这个数据库由 PC 端构建工具从 Home Assistant Core、HA generated 数据、integration manifest、允许复用的第三方设备库和标准厂商数据自动生成。

ESP32 运行时只做：

```text
scan result
 -> 小型 matcher key
 -> SD 索引查询
 -> 返回 integration/profile id + metadata
 -> 已编译 parser / 标准协议映射
 -> Device / Entity
```

不在 ESP32 上解析几千份 JSON，不在 SD 上频繁写数据库。

---

## 1. 为什么需要 SD 数据库

ESP32-C6 目标板只有有限 Flash/RAM，而识别知识会持续增长：

- Home Assistant Bluetooth manifest matcher
- Zeroconf / HomeKit matcher
- SSDP matcher
- DHCP hostname / MAC matcher
- IEEE OUI / vendor prefixes
- Matter Vendor ID / Product ID
- Zigbee manufacturer/model/profile tables
- BLE company identifiers / service UUID mapping
- 各 integration 的 model -> device class/entity metadata
- 第三方 parser 库中的静态 product tables

这些数据大多是“冷数据”：扫描时只查一小部分，不需要全部进 RAM。

1GB SD 对纯识别 metadata 来说空间非常充裕，并且给未来数据库版本、许可证文本、调试资源和可选 PCAP 导出留出余量。

---

## 2. 必须区分：数据 vs 代码

SD 数据库只能很好解决 **声明式识别知识**，不能默认替代 executable parser code。

### 非常适合数据库化

- Bluetooth matcher：local name / UUID / manufacturer id / manufacturer-data prefix
- Zeroconf service type / name / TXT property matcher
- HomeKit model matcher
- SSDP header matcher
- DHCP hostname / MAC-OUI matcher
- IEEE OUI vendor mapping
- BLE SIG company/service/appearance tables
- Matter VID/PID -> manufacturer/model metadata
- Zigbee manufacturer/model -> integration/profile metadata
- icon/name/device_class/unit 等静态 Entity 描述
- product/model alias tables
- parser 中纯静态 lookup tables

### 通常必须编译进固件

- Xiaomi BLE 等复杂 payload decoder
- SwitchBot proprietary packet parser
- 需要 AES/CRC/bitfield/state-machine 的 vendor parser
- Matter/Zigbee/BLE GATT 实际交互代码
- Integration 的控制命令编码

**Agent A 的后续审计任务：逐 integration 标出 `DATA_ONLY / DATA+PARSER / CODE_ONLY`。**

不要为了把代码也放 SD 而引入解释器、插件 VM、动态加载器；这会违背“尽量不写新功能”的项目原则。

---

## 3. 数据源优先级

### A. Home Assistant Core

直接抽取：

```text
homeassistant/components/*/manifest.json
homeassistant/generated/*
script/hassfest/*
homeassistant/loader.py matcher schema
```

这些是第一数据库源。

### B. HA integration requirements

对于每个 integration：

1. 读取 manifest `requirements`。
2. 找第三方 parser repo。
3. 检查许可证。
4. 提取其中纯静态 device/model tables。
5. 若 decoder 很小且许可允许，进入固件 parser candidate。

### C. 标准公开数据

按许可证确认后可加入：

- IEEE OUI
- Bluetooth SIG company identifiers / assigned numbers
- Matter vendor/product metadata来源
- Zigbee manufacturer identifiers / cluster metadata来源

所有来源必须保留 provenance/license metadata，数据库生成器输出对应 notices。

---

## 4. 不建议把 HA 原始 JSON 原样塞 SD

可以做，但不是最佳 runtime 形式。

问题：

- JSON 文本体积大
- 每次 parse 消耗 RAM/CPU
- 字符串重复严重
- matcher 需要大量线性遍历
- MCU 上动态对象分配容易碎片化

建议 PC 构建期生成只读二进制格式。

```text
HA / third-party upstream
       -> Python generator
       -> normalize
       -> deduplicate strings
       -> build indexes
       -> nearby-db-v1 binary files
```

ESP32 不需要知道上游是 JSON、Python 还是 YAML。

---

## 5. 数据库格式候选

Agent A 后续要实际 benchmark 以下三类。

### 方案 A：SQLite read-only

优点：

- 成熟
- 索引/查询方便
- PC 端生成非常容易
- 数据更新/inspect 工具多

缺点：

- ESP32 SQLite library 会占额外 Flash
- page cache 占 RAM
- FATFS/SD 随机访问成本
- 我们查询模式其实很简单，SQL 可能过重

适合做 baseline/prototype，但未必是最终方案。

### 方案 B：CBOR / MessagePack 分片

优点：实现简单、比 JSON 紧凑、调试方便。

缺点：仍需要 decode；索引能力弱，可能要额外 index。

适合作为生成中间格式，不一定适合最终高频 matcher。

### 方案 C：自定义只读 flat binary + sorted indexes

推荐作为最终候选。

示例：

```text
index.bin
  magic/version
  sections[]
  string-table offset

bluetooth.bin
  manufacturer_id index
  service_uuid index
  local_name prefix index
  records[]

zeroconf.bin
  service_type index
  property matcher records[]

oui.bin
  sorted prefix -> vendor string id
```

查询方式尽量简单：

```text
binary search / prefix bucket
 -> seek fixed offset
 -> read tiny record
```

只把顶级 section index / 热门 bucket cache 放 RAM。

优点：

- runtime 代码很小
- RAM 极低
- 不需要通用 DB engine
- 文件可直接按协议按需打开

缺点：需要写一个 generator 和很薄的 reader；但这是“一次性胶水”，符合项目原则。

---

## 6. 推荐的 runtime 架构

```text
                    RAM Session
                  Device / Entity
                        ^
                        |
                 integration parser
                        ^
                        |
ESP-IDF scan -> HA matcher runtime
                        |
                 hot index cache
                        |
                        v
                  SD Recognition DB
```

关键原则：

1. **不把整库加载 RAM。**
2. **按协议分片。** BLE 扫描时不打开 SSDP/Zigbee 大表。
3. **顶层 key 预索引。** manufacturer ID / UUID / service type / OUI 等先缩小候选。
4. **字符串去重。** manufacturer/model/integration/domain 使用 string table id。
5. **只读。** 每次开机不写识别库。
6. **Session 数据不进 SD。** 设备发现历史仍然关机即丢。
7. **版本化。** DB header 带 schema version、HA upstream commit、build timestamp。
8. **许可可追踪。** 每个 section/record group 能回溯 source/license。

---

## 7. 数据库更新模式

第一版不要做 OTA 数据库更新协议。

最简单：

```text
PC generator
 -> nearby-db-v1/
 -> 拷到 SD 卡
 -> 开机校验版本
```

固件只检查：

```text
magic
schema version
CRC/hash
required sections
```

未来如果需要更新，再复用已有 HTTP/OTA/SD 文件替换能力，不提前发明新更新系统。

---

## 8. 与 Home Assistant 上游同步

建议仓库未来加入：

```text
tools/dbgen/
  extract_ha.py
  extract_requirements.py
  normalize.py
  build_bt.py
  build_zeroconf.py
  build_ssdp.py
  build_dhcp.py
  build_oui.py
  license_report.py
```

但尽量复用 `hassfest` 现有生成逻辑，而不是自己重新理解所有 manifest。

最终构建流程：

```text
pin HA commit/tag
 -> run extractor
 -> collect third-party static tables
 -> license gate
 -> generate DB
 -> generate parser inclusion list
 -> build firmware
```

DB 与 firmware 应记录同一个 generation manifest，避免 DB 指向一个固件没有编译的 parser。

---

## 9. 需要重点解决的 parser coverage 问题

真正的容量风险不一定是 matcher 数据，而是 parser 数量。

因此 Agent A 下一轮必须统计：

```text
integration
  discovery type
  matcher count
  third-party requirement
  static table size
  parser language
  parser license
  parser estimated code size
  DATA_ONLY / DATA+PARSER / CODE_ONLY
```

然后找出：

- 只靠数据库即可支持的 integration
- 使用标准协议、无需 vendor parser 的 integration
- parser 足够小可直接裁剪的 integration
- parser 太大/依赖太重，只做“识别但不可交互”的 integration

这将决定 NearBy 的实际设备覆盖率。

---

## 10. 第一版建议

先不要上完整 SQLite。

做一个最小 DB proof-of-concept：

```text
nearby-db-v0/
  bluetooth.bin   # HA bluetooth manifest matcher
  zeroconf.bin    # HA zeroconf + HomeKit matcher
  oui.bin         # vendor prefix
  strings.bin
```

测试链路：

```text
NimBLE advertisement
 -> manufacturer ID / service UUID
 -> SD lookup
 -> xiaomi_ble / switchbot / shelly ... integration id
 -> 若 firmware 有对应 parser，则解析
 -> Device/Entity
```

同时用 PC 端 Python 版本的同一 matcher 数据作为 oracle，验证 ESP32 查询结果完全一致。

如果这个模型成立，再加入 SSDP/DHCP/Matter/Zigbee。

## 11. 当前建议

**做，而且应该尽早做。**

NearBy One NEXT 的核心资产很可能最终不是扫描器本身，而是：

```text
HA-compatible scanner inputs
        +
大型 SD Recognition Database
        +
一组尽量直接裁剪的 integration parsers
        +
HA Device / Entity / State
```

SD 卡使“尽可能继承 Home Assistant 的庞大识别知识”真正可行，同时不会迫使我们在 8MB Flash 中塞入大量冷数据。
