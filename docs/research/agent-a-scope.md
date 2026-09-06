# Agent A — Home Assistant Core 语义层拆解范围

Agent A 现在只聚焦 Home Assistant Core 的设备组织与 UI 适配基础语义，不再负责大型设备识别数据库。

## 核心目标

尽最大可能复用/照搬 Home Assistant 原有思路与命名，明确 NearBy One NEXT 在 ESP32-C6 上真正需要保留的最小子集：

```text
Integration
  -> Platform
  -> Device
  -> Entity
  -> State + Attributes
  -> UI
```

## 第一优先级

1. `Device` / Device Registry
   - identifiers
   - connections
   - manufacturer/model/name
   - config entry 关联
   - Device 与 Entity 的归属关系
   - transient RAM-only 场景下哪些 registry 逻辑可以删除

2. `Entity` / Entity Registry
   - entity_id / unique_id / platform / domain
   - device_id
   - Entity lifecycle
   - enabled/available/state 属性
   - domain-specific entity base classes 的最小公共部分

3. `State` / State Machine
   - state
   - attributes
   - state update / listener
   - UI 如何只消费 domain + state + attributes

4. Integration / Platform
   - integration 如何创建 Device/Entity
   - platform setup / add_entities
   - discovery 到 integration 再到 entity 的标准链路
   - service/action 如何回到 entity/integration

5. HA domain 语义
   - `sensor`
   - `binary_sensor`
   - `switch`
   - `light`
   - `button`
   - 后续按需要追加 HA 已有 domain，不创造 NearBy 自定义 Capability 类型

## 明确不做

- SD Recognition Database 设计（Agent C）
- ZHA quirks / Zigbee2MQTT / ESPHome Devices 数据抽取（Agent C）
- scanner / packet parser（Agent B）
- HA frontend 整体移植
- HA persistence/history/recorder
- config storage / long-term registry migration
- 新的 Capability / ViewModel / DeviceGraph 抽象

## Agent A 最终应交付

- HA `Device / Entity / State / Registry / Integration / Platform` 源码地图
- 哪些代码/结构能直接复制，哪些需要 C 等价实现
- transient RAM-only 模式下的删减清单
- 最小 C struct / API 映射，字段名尽量沿用 HA
- `sensor / binary_sensor / switch / light / button` 第一批 domain 的状态与属性表
- 从 integration 创建 Device/Entity 到 UI 读取 State 的完整纵向 PoC 设计

目标不是“设计一个类似 HA 的框架”，而是**尽可能忠实地做 HA Core 设备语义的 MCU 子集**。
