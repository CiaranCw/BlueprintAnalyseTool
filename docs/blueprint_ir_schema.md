# Blueprint IR Schema 说明

本文档说明 Blueprint IR 的字段语义。**Schema 版本：`0.1.0`**。

> **权威字段位置与一致性约定（analyze/create 产出的统一 IR，务必按此读取）**
> - **资产级字段在 `asset.*`**：`asset.parent_class` / `asset.generated_class` / `asset.implemented_interfaces` /
>   `asset.dependencies` / `asset.asset_type`。**不要**从 `blueprint.*` 读父类/生成类。
> - **成员级字段在 `blueprint.*`**：`blueprint.variables` / `functions` / `macros` / `event_dispatchers` /
>   `components` / `graphs`。**计数以 `blueprint.*` 为准**（根级不再放重复的空数组）。
> - **结构对称**：`variables / functions / macros / event_dispatchers` 全部为**对象数组**，每项至少含
>   `{ "name": ... }`（可安全地 `entry.name`，不会因某个字段是纯字符串数组而崩溃）。
> - **编码**：所有 `*.json` 产物为**无 BOM UTF-8**，可被 `json.load` / `JSON.parse` 直接读取（无需 `utf-8-sig`）。
> - **manifest 与 IR 同源**：`manifest.parent_class/generated_class` 与 `asset.parent_class/generated_class` 取自同一来源，保持一致。
> - **understanding_score 语义**：`*_discovery` 表示「该模式是否完整抽取了该类别」，**不是**「是否有条目」。
>   `native_full` 下某字段为 `complete` 但计数为 0，表示资产**本就没有**该类别（N/A），而非解析不全；空集见 `empty_categories`。

JSON Schema 定义文件位于：

```text
agent_tools/schemas/
├─ blueprint_ir.schema.json          完整 IR（聚合）
├─ blueprint_manifest.schema.json    manifest 层
├─ graph_summary.schema.json         图概要层
├─ node_detail.schema.json           节点详情层
└─ subgraph_slice.schema.json        切片输出
```

> 所有 IR 字段都必须能回溯到 UE 内部对象。下文每个字段都标注其 **UE 来源**。

---

## 1. 资产级（Manifest 顶层）

| 字段                            | 类型             | UE 来源                                                           | 说明                                              |
| ------------------------------- | ---------------- | ----------------------------------------------------------------- | ------------------------------------------------- |
| `schema_version`                | string (semver)  | 工具自身                                                          | IR Schema 版本                                    |
| `asset.asset_path`              | string           | `UBlueprint::GetPathName()`（去掉 ObjectName）                     | `/Game/Blueprints/BP_Hero`                         |
| `asset.package_path`            | string           | `UBlueprint::GetPathName()`                                        | `/Game/Blueprints/BP_Hero.BP_Hero`                 |
| `asset.asset_name`              | string           | `UBlueprint::GetName()`                                            |                                                   |
| `asset.blueprint_class`         | string           | `UBlueprint::GetClass()->GetName()`                                | `Blueprint` / `WidgetBlueprint` / `AnimBlueprint` |
| `asset.generated_class`         | string           | `UBlueprint::GeneratedClass->GetPathName()`                        |                                                   |
| `asset.parent_class`            | string           | `UBlueprint::ParentClass->GetPathName()`                           | reparent (`set_parent_class`) changes this; edit `diff_report.modified_asset.parent_class{before,after}` |
| `asset.blueprint_type`          | string           | `UBlueprint::BlueprintType`                                        | `BPTYPE_Normal` / `BPTYPE_Interface` / ...        |
| `asset.engine_version`          | string           | `FEngineVersion::Current().ToString()`                             |                                                   |
| `asset.plugin_dependencies`     | string[]         | `UBlueprint::Dependencies` 解析所属插件                            |                                                   |
| `asset.source_fingerprint`      | object           | 文件系统 + 哈希                                                    | `{uasset_size, uasset_mtime, uasset_sha256}`      |
| `asset.parse_time`              | string (ISO8601) | `FDateTime::UtcNow()`                                              |                                                   |
| `asset.parse_status`            | enum             | 工具                                                               | `success` / `partial` / `fatal`                   |
| `asset.warnings`                | object[]         | reader 累积                                                        | `{code, message, where}`                          |
| `asset.errors`                  | object[]         | reader 累积                                                        | 同上                                              |

---

## 2. 成员（`members`）

### 2.1 `variables[]`

| 字段              | UE 来源                                          |
| ----------------- | ------------------------------------------------ |
| `name`            | `FBPVariableDescription::VarName`                |
| `guid`            | `FBPVariableDescription::VarGuid`                |
| `type`            | `FBPVariableDescription::VarType`                |
| `default_value`   | `FBPVariableDescription::DefaultValue`           |
| `is_replicated`   | `FBPVariableDescription::ReplicationCondition` 等 |
| `category`        | `FBPVariableDescription::Category`               |

### 2.2 `functions[]`

| 字段              | UE 来源                                          |
| ----------------- | ------------------------------------------------ |
| `name`            | `UEdGraph::GetFName()`                           |
| `graph_id`        | 工具内部分配                                     |
| `is_pure`         | 函数图入口节点 `UK2Node_FunctionEntry::FunctionFlags & FUNC_BlueprintPure` |
| `is_const`        | `FUNC_Const`                                     |
| `signature`       | 入口节点的 Pin 列表归一化                        |

### 2.3 `macros[]`

字段同 functions[]，UE 来源换为 `UBlueprint::MacroGraphs`。

### 2.4 `event_dispatchers[]`

UE 来源：`UBlueprint::DelegateSignatureGraphs`。

### 2.5 `implemented_interfaces[]`

UE 来源：`UBlueprint::ImplementedInterfaces[].Interface->GetPathName()`。

### 2.6 `timelines[]`

UE 来源：`UBlueprint::Timelines`。

### 2.7 `components`（仅 Actor 类）

UE 来源：`UBlueprint::SimpleConstructionScript->GetRootNodes()` 递归。每个节点：

| 字段       | UE 来源                                                |
| ---------- | ------------------------------------------------------ |
| `node_id`  | 工具分配（`scs_<index>`）                              |
| `name`     | `USCS_Node::GetVariableName()`                         |
| `class`    | `USCS_Node::ComponentClass->GetPathName()`             |
| `children` | `USCS_Node::GetChildNodes()` 递归                      |

---

## 3. 图（`graphs[]`）

| 字段              | UE 来源                                                     |
| ----------------- | ----------------------------------------------------------- |
| `graph_id`        | 工具分配（稳定 hash from GraphName + Owner）                |
| `graph_name`      | `UEdGraph::GetFName()`                                       |
| `graph_type`      | `ubergraph` / `function` / `macro` / `delegate` / `intermediate` / `widget` / `anim` |
| `graph_owner`     | 所属蓝图 path                                                |
| `node_count`      | `UEdGraph::Nodes.Num()`                                      |
| `pin_count`       | sum of node->Pins.Num()                                      |
| `edge_count`      | EdgeResolver 输出条数                                        |
| `entry_nodes`     | `UK2Node_Event` / `UK2Node_FunctionEntry` 等入口节点 id     |
| `local_variables` | `UEdGraph::LocalVariables`（仅函数图）                      |
| `summary_path`    | 工具相对路径                                                 |
| `graph_warnings`  | EdgeResolver / NodeReader 累积                              |

---

## 4. 节点（Node Detail）

| 字段                  | UE 来源                                                    |
| --------------------- | ---------------------------------------------------------- |
| `node_id`             | 工具分配（稳定 hash from `NodeGuid`）                      |
| `node_guid`           | `UEdGraphNode::NodeGuid`                                   |
| `node_class`          | `UEdGraphNode::GetClass()->GetName()`（如 `K2Node_CallFunction`） |
| `node_title`          | `UEdGraphNode::GetNodeTitle(ENodeTitleType::FullTitle)`    |
| `node_comment`        | `UEdGraphNode::NodeComment`                                |
| `node_position`       | `{ x: NodePosX, y: NodePosY }`                             |
| `node_enabled_state`  | `UEdGraphNode::EnabledState`                               |
| `node_type_category`  | 工具分类：`event` / `function_call` / `variable_get` / `variable_set` / `cast` / `branch` / `composite` / `macro_instance` / `delegate` / `unknown` |
| `function_reference`  | `UK2Node_CallFunction::FunctionReference`（若适用）        |
| `variable_reference`  | `UK2Node_Variable::VariableReference`                      |
| `event_reference`     | `UK2Node_Event::EventReference`                            |
| `macro_reference`     | `UK2Node_MacroInstance::MacroGraphReference`               |
| `pins[]`              | 见第 5 节                                                  |
| `metadata`            | `UEdGraphNode::AdvancedPinDisplay` 等扩展位                 |
| `graph_id`            | 工具回填                                                   |
| `owner_asset`         | 工具回填                                                   |
| `reverse_hints`       | 当前阶段恒为 `null`，反向阶段使用                          |

---

## 5. Pin

| 字段                  | UE 来源                                                |
| --------------------- | ------------------------------------------------------ |
| `pin_id`              | 工具分配（`pin_<nodeIdx>_<pinIdx>` 或 hash from PinId） |
| `pin_name`            | `UEdGraphPin::PinName`                                  |
| `direction`           | `UEdGraphPin::Direction`（input/output）                |
| `category`            | `UEdGraphPin::PinType.PinCategory`                      |
| `sub_category`        | `UEdGraphPin::PinType.PinSubCategory`                   |
| `object_type`         | `UEdGraphPin::PinType.PinSubCategoryObject->GetPathName()` |
| `container_type`      | `UEdGraphPin::PinType.ContainerType`（none/array/set/map） |
| `is_exec`             | `UEdGraphSchema_K2::IsExecPin`                          |
| `is_data`             | `!is_exec`                                              |
| `default_value`       | `UEdGraphPin::DefaultValue`                             |
| `default_object`      | `UEdGraphPin::DefaultObject->GetPathName()`             |
| `linked_to`           | `UEdGraphPin::LinkedTo` 解析为 `[{node_id, pin_id}]`     |

---

## 6. 边（Edge）

边在内存中是独立表，序列化优先放 `graphs/<id>.full.json`（小图）或 `graphs/<id>.edges.json`（大图分文件）。

| 字段              | 类型 / UE 来源                                                  |
| ----------------- | --------------------------------------------------------------- |
| `edge_id`         | 工具分配                                                        |
| `from_node_id`    | NodeReader id                                                   |
| `from_pin_id`     | PinReader id                                                    |
| `to_node_id`      | 同                                                              |
| `to_pin_id`       | 同                                                              |
| `edge_kind`       | `exec` / `data` / `delegate` / `latent_continuation` / `unknown` |
| `type_info`       | 与 `from_pin` 的 `PinType` 同结构                               |
| `metadata.is_latent_continuation` | bool，UE 来源：`UFunction::HasMetaData(TEXT("Latent"))`【待验证 V3】 |

> Latent 边判定依赖于 `UK2Node_CallFunction::GetTargetFunction()` 上的 `Latent` metadata；
> 若节点不是 K2Node_CallFunction（例如自定义节点声明了 latent 行为），当前阶段标 `edge_kind=exec` + `metadata.is_latent_continuation=null`，并加 warning。

---

## 7. 警告与错误

`asset.warnings[]` / `asset.errors[]` / `graph.graph_warnings[]` 内统一格式：

```json
{
  "code": "BP_W_UNKNOWN_NODE_CLASS",
  "level": "warning",
  "where": { "graph_id": "graph_0001", "node_id": "node_0042" },
  "message": "Unsupported node class K2Node_CustomFoo, recorded as unknown."
}
```

错误码命名：`BP_E_*` 表示 error，`BP_W_*` 表示 warning。完整列表维护在 `agent_tools/schemas/error_codes.schema.json`。

---

## 8. 反向阶段预留字段

```json
"reverse_generation_reservation": {
  "construction_recipe": null,
  "compile_options": null,
  "node_creation_hints": null,
  "pin_default_overrides": null,
  "current_phase_writes_them": false
}
```

每个 node 也保留 `reverse_hints: null`，每条 edge 保留 `creation_order_hint: null`。
当前阶段全部为 `null`，反向阶段不破坏 `schema_version` 主版本。

---

## 9. 字段稳定性约定

- `schema_version` 采用 SemVer；`0.x.y` 内允许新增字段，重命名 / 语义变更 → bump minor。
- `node_id` / `pin_id` / `edge_id` 必须**在同一蓝图同一 schema 版本下稳定**：相同输入应产出相同 id，便于 IR diff 与 golden 测试。
- 不允许字段同名但语义改变。

---

## 10. widget_tree（仅 Widget Blueprint / UMG）

当资产是 `UWidgetBlueprint` 时，根级额外输出 `widget_tree`，镜像 `UWidgetTree` 的可视化层级
（UE 来源：`UWidgetBlueprint::WidgetTree` → `UWidget` / `UPanelWidget` / `UPanelSlot`）。

```json
"widget_tree": {
  "root": {
    "name": "RootCanvas",                     // UWidget::GetName()
    "class": "/Script/UMG.CanvasPanel",        // UWidget::GetClass()->GetPathName()
    "is_variable": true,                        // UWidget::bIsVariable
    "slot": {                                   // 该控件在其父面板中的槽（无父则为 null）
      "class": "/Script/UMG.CanvasPanelSlot",
      "properties": { "LayoutData": "(Offsets=(Left=40.000000,Top=40.000000,Right=400.000000,Bottom=60.000000))" }
    },
    "properties": { "Text": "Main Menu", "Visibility": "HitTestInvisible" },
    "children": [ { "name": "...", "class": "...", "slot": {}, "properties": {}, "children": [] } ]
  }
}
```

约定：
- `properties` / `slot.properties` 仅包含**相对类默认值发生改变**的可编辑属性（`CPF_Edit` 且与 CDO 不同），
  以 `ExportText` 字符串形式给出，保证紧凑且能反映 create/edit 实际写入的值（便于 expected↔actual 对比）。
- 结构性反向引用（`Slot` / `Slots`）不进入 `properties`（槽已作为独立 `slot` 节点，子级即 `children`）。
- Canvas 槽的 Position/Size/Anchors/Alignment 落在 `LayoutData`（通过 setter 写入）；Box 槽的
  `Padding`/`Size`/对齐为直接属性。

每个 widget 节点还含 **`bindable_events`**（反射枚举控件类上 BlueprintAssignable 的多播委托 = UMG “+ event” 可绑定项）：
```json
"bindable_events": [ { "event_name": "OnClicked", "delegate_property": "OnClicked", "parameters": [] },
                     { "event_name": "OnCheckStateChanged", "delegate_property": "OnCheckStateChanged",
                       "parameters": [ { "name": "bIsChecked", "type": "bool" } ] } ]
```

每个 widget 节点还含 **`settable_properties`** 与 **`slot_settable_properties`**（见 10.3）——用于让其他 AI
在 create/edit 前**准确得知控件/槽可设置的真实字段名**，避免用 Details 显示名猜错。

### 10.1 settable_properties / slot_settable_properties（每个 widget 节点 + WBP 根级）
枚举控件类（**含继承链**）上 `CPF_Edit`（Details 可编辑）的属性——控件自身 C++ UPROPERTY、继承的引擎属性、
自定义 `UserWidget` 暴露的变量。**排除**：委托（在 `bindable_events`）、函数、结构性反向引用（`Slot`/`Slots`）。
WBP 根级的 `settable_properties` 为该 WBP **自身生成类**的可设置属性（analyze 一个自定义控件即可知道能在其实例上设什么）。
```json
"settable_properties": [
  { "name": "bDefaultChecked",              // 真实 FProperty 内部名（写请求时用它）
    "display_name": "Default Checked",       // Details 显示名（FProperty::GetDisplayNameText，可能与 name 不同）
    "type": { "category": "bool", "sub_category": "", "sub_category_object": "", "container_type": "none" },
    "declaring_class": "/Script/AClient.RGSettingsCheckboxItemWidget",  // 声明该属性的类（继承链定位）
    "editable": true, "blueprint_visible": true, "blueprint_read_only": false, "deprecated": false,
    "current_value": "False",                // 实例（或根级 CDO）上的 ExportText 值
    "set_supported": true,                   // 是否支持反射写入
    "notes": [] }                            // set_supported=false 时: readonly_or_internal | transient | deprecated
],
"slot_settable_properties": [ /* 同结构，枚举该控件所在 slot 对象的可编辑属性 */ ]
```
要点：
- **务必用 `name`（内部名）而非 `display_name`** 构造 create/edit 请求；bool 属性常带 `b` 前缀（`bDefaultChecked`）。
- `type.category` ∈ `bool|int|int64|float|double|string|name|text|byte|enum|struct|class|object|soft_object|unknown`，
  容器用 `container_type` ∈ `none|array|set|map`，`sub_category_object` 给出 enum/struct/class 的路径。
- create/edit 若属性名匹配失败，`manifest`/`create_result` 的 `property_notes` 会给出 `property_not_found` +
  `suggestions`（候选 `{name,display_name,type}`）；若走了 alias 匹配则给出 `property_alias_matched`
  （`input`→`resolved_to`）。见 `docs/issue_patterns.md` P16。

### 10.2 widget_event_bindings（Widget Blueprint 根级）
已在图中创建的绑定事件节点（`UK2Node_ComponentBoundEvent`）的 redump（UE 来源：遍历 `UbergraphPages`/`FunctionGraphs`），
每条含 **`handler`**（Phase 4 P2 —— 事件 exec/参数接线的真实回读，沿 bound-event 的 `then` 追踪到下游 Call 节点）：
```json
"widget_event_bindings": [
  { "widget": "QualityCombo", "event": "OnSelectionChanged", "delegate_property": "OnSelectionChanged",
    "node_class": "K2Node_ComponentBoundEvent", "node_title": "On Selection Changed (QualityCombo)",
    "graph": "EventGraph", "parameters": [ "SelectedItem", "SelectionType" ], "status": "bound",
    "handler": {
      "type": "function",                 // bound_event | custom_event | function
      "name": "HandleQualityChanged",
      "connected": true,                    // handler 已接到 bound event（exec）
      "exec_connected": true,
      "parameters_connected": [
        { "from": "SelectedItem",  "to": "SelectedItem",  "status": "connected" },
        { "from": "SelectionType", "to": "SelectionType", "status": "connected" }
      ]
    } }
]
```
`handler.type` 判定：下游 Call 节点目标是图中同名 `UK2Node_CustomEvent` → `custom_event`，否则 `function`；无下游 Call →
`bound_event`（bound-event 自身即入口）。参数 `status`：`connected | not_connected`（redump 视角）。

create 侧另在 `manifest.json`/`create_result.json` 写入 `widget_event_bindings`（含每个请求事件的绑定
`status`：`bound|reused|widget_not_found|not_variable|property_missing|delegate_not_found|pins_incomplete|error`
以及 `handler` 对象，其 `status`/`parameters_connected[].status` 覆盖接线分类：`connected|already_connected|
parameter_pin_missing|parameter_type_mismatch|ambiguous_parameter_match`，handler 级失败：`handler_not_found|
handler_create_failed|function_is_pure|exec_pin_missing|exec_connection_failed`）。两者可交叉核对（请求结果 vs 图内实际节点）。
Graph IR（`graphs[]`）中也能看到真实的 Custom Event / Call Function 节点与 exec/data 边。

### 10.3 dependencies（Widget Blueprint 根级 — 自定义控件）
WidgetTree 中引用的项目自定义控件（class path 以 `/Game/` 开头）会去重记录：
```json
"dependencies": [
  { "type": "custom_user_widget", "asset_path": "/Game/UI/Common/WBP_CustomButton",
    "generated_class": "/Game/UI/Common/WBP_CustomButton.WBP_CustomButton_C" }
]
```
dumper 从 widget_tree 派生（analyze/redump 通用）；create 侧另按解析结果写入 manifest/create_result 的 `dependencies`。
