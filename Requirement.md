# 任务目标

你需要作为一名熟悉Unreal Engine编辑器扩展、蓝图系统、Agent工程化设计、C++Editor插件、Commandlet、结构化IR设计的高级工程师，帮助我设计并构建一个用于解析UE蓝图文件的Agent底层工具项目。

这个项目的最终目标是：让后续Agent能够“看懂”UE蓝图结构。所谓“看懂”，不是让大模型直接读取`.uasset`二进制文件，而是通过UE自身的编辑器能力加载蓝图资产，解析出可靠的结构化中间表示，然后让Agent基于该中间表示进行查询、检索、解释和后续推理。

当前阶段只实现**正向过程**，也就是：

```text
UE蓝图资产 → UE内部加载 → 结构化解析 → Blueprint IR导出 → 索引与切片 → Agent可查询文档和接口
```

本阶段暂不实现反向过程，也就是暂不要求：

```text
自然语言/BlueprintSpec → 构造蓝图 → 生成`.uasset` → 编译通过
```

反向构建蓝图能力留到下一阶段。当前所有设计都要为下一阶段预留接口，但不能把当前阶段复杂化。

---

# 核心要求

## 1. 拆分原子能力

你需要把整个项目拆分成多个原子能力。每个原子能力都必须满足：

- 必要，不是为了显得复杂而增加模块
- 不冗余，不能多个模块做同一件事
- 规模合理，一个原子能力只负责一类明确任务
- 输入输出清晰
- 可以被单独测试
- 可以被后续Agent无障碍调用
- 可以集成为完整的蓝图解析流程

你需要明确说明每个原子能力：

- 名称
- 职责
- 输入
- 输出
- 依赖
- 是否会读取原始UE资产
- 是否会修改原始UE资产
- 测试方式
- 为什么它是必要的
- 为什么它不应该和其他能力合并

---

## 2. 集成原子能力实现最终需求

你需要把原子能力集成为一条完整流程：

```text
指定UE工程和蓝图资产路径
    ↓
通过UEEditor/UnrealEditor-Cmd加载蓝图资产
    ↓
解析蓝图基础信息
    ↓
解析图Graph
    ↓
解析节点Node
    ↓
解析Pin
    ↓
解析Pin连接
    ↓
解析变量、函数、宏、事件、组件树
    ↓
导出Blueprint IR
    ↓
建立索引
    ↓
支持按需切片
    ↓
输出Agent可查询文档和接口
```

这条流程需要支持后续Agent调用，而不是只能人手工看结果。

因此你需要设计：

- 命令行调用方式
- JSON输入参数格式
- JSON输出结果格式
- 错误码规范
- 日志规范
- 输出目录规范
- Agent查询接口
- 文档生成方式

---

## 3. 项目必须可以无痛集成到Agent内部

该项目不是一个孤立的UE插件，而是要作为Agent的底层工具能力。

因此你需要设计出后续Agent可以直接使用的接口，例如：

```text
list_blueprint_assets(project)
dump_blueprint(asset_path)
get_blueprint_manifest(asset_path)
list_graphs(asset_path)
get_graph_summary(asset_path, graph_name)
search_nodes(asset_path, keyword)
get_node_detail(asset_path, node_id)
trace_exec_flow(asset_path, start_node_id, max_depth)
trace_variable_usage(asset_path, variable_name)
slice_subgraph(asset_path, start_node_id, direction, depth)
validate_blueprint_ir(asset_path)
```

你需要明确每个接口：

- 用途
- 输入
- 输出
- 失败情况
- 是否需要调用UE
- 是否可以只读缓存
- 适合Agent在什么场景下调用

同时需要为后续Agent提供文档，包括：

- 项目总体说明
- 原子能力说明
- Blueprint IR字段说明
- Agent工具调用说明
- 大蓝图切片策略说明
- 正确性校验说明
- 不允许操作说明
- 后续反向构建蓝图阶段的预留接口说明

---

## 4. 不允许破坏原始UE工程文件

这是最高优先级约束。

整个正向解析阶段必须是只读流程。除非明确生成到独立输出目录，否则不能修改任何原始文件。

禁止行为包括：

- 不能修改原始`.uasset`
- 不能保存原始蓝图资产
- 不能自动重编译并保存原始蓝图
- 不能改动`Content/`下已有资产
- 不能修改项目原有源码
- 不能修改项目原有配置文件
- 不能删除任何原始目录
- 不能覆盖已有导出结果，除非显式指定`--overwrite`
- 不能把临时文件写入原始资产目录
- 不能依赖大模型直接解析`.uasset`二进制内容

允许行为包括：

- 在独立目录中创建插件源码
- 在独立输出目录中生成IR文件
- 在独立输出目录中生成索引文件
- 在独立输出目录中生成日志文件
- 在独立输出目录中生成文档
- 使用UE Editor只读加载资产
- 使用AssetRegistry枚举资产
- 使用Commandlet批量导出结构

所有工具默认必须以只读模式运行。任何可能写入UE资产的API都必须显式禁止或隔离。

---

## 5. 首先实现正向蓝图解析过程

当前阶段只做蓝图解析，不做蓝图生成。

正向解析过程必须具备足够通用性，不能只针对一个普通Actor蓝图。

需要考虑并尽量兼容以下蓝图类型：

- 普通Actor Blueprint
- Pawn Blueprint
- Character Blueprint
- ActorComponent Blueprint
- SceneComponent Blueprint
- Widget Blueprint
- Animation Blueprint
- Blueprint Interface
- Macro Library
- Function Library
- Data-only Blueprint
- Level Blueprint，若当前阶段支持困难，需要明确标注限制
- 其他继承自UBlueprint或相关子类的蓝图资产

你需要先研究这些蓝图类型在UE中的结构差异，然后设计统一的Blueprint IR。

如果某些类型第一阶段无法完整支持，不能假装支持。你需要：

- 明确说明当前支持程度
- 标记为partial support
- 输出可检测的warning
- 保证不会解析错误
- 保证不会破坏资产
- 给出后续扩展点

---

# 技术路线要求

## 不要直接解析`.uasset`

不要尝试从外部直接读取`.uasset`二进制并还原蓝图结构。该方式不可靠，无法保证跨UE版本正确，也无法保证Pin连接、节点类型和编译语义准确。

正确路线是：

```text
UE Editor或UnrealEditor-Cmd
    ↓
UE对象系统加载资产
    ↓
UBlueprint / UWidgetBlueprint / UAnimBlueprint等对象
    ↓
UEdGraph / UEdGraphNode / UEdGraphPin
    ↓
导出结构化Blueprint IR
```

核心解析工具优先使用：

```text
UE C++ Editor插件
+
自定义Commandlet
```

可以辅助使用：

```text
Unreal Python
AssetRegistry
JSON/SQLite
外部Python脚本做索引和文档生成
```

但是核心蓝图结构解析必须基于UE内部对象，而不是让LLM或外部库猜测`.uasset`格式。

---

# 推荐项目结构

你需要研究后给出最终目录结构。可以参考但不必机械照抄下面结构：

```text
BlueprintAgentTools/
├─ README.md
├─ docs/
│  ├─ architecture.md
│  ├─ atomic_capabilities.md
│  ├─ blueprint_ir_schema.md
│  ├─ agent_tools.md
│  ├─ slicing_strategy.md
│  ├─ validation_strategy.md
│  ├─ readonly_safety.md
│  └─ roadmap_reverse_generation.md
│
├─ ue_plugin/
│  └─ BlueprintAgentTools/
│     ├─ BlueprintAgentTools.uplugin
│     └─ Source/
│        ├─ BlueprintAgentTools/
│        │  ├─ BlueprintAgentTools.Build.cs
│        │  ├─ Public/
│        │  └─ Private/
│        │
│        └─ BlueprintAgentToolsEditor/
│           ├─ BlueprintAgentToolsEditor.Build.cs
│           ├─ Public/
│           │  ├─ Commandlets/
│           │  ├─ Exporters/
│           │  ├─ Readers/
│           │  ├─ Validators/
│           │  └─ Schema/
│           └─ Private/
│              ├─ Commandlets/
│              ├─ Exporters/
│              ├─ Readers/
│              ├─ Validators/
│              └─ Schema/
│
├─ agent_tools/
│  ├─ README.md
│  ├─ blueprint_tool_manifest.json
│  ├─ python/
│  │  ├─ blueprint_ir_client.py
│  │  ├─ blueprint_indexer.py
│  │  ├─ blueprint_slicer.py
│  │  ├─ blueprint_query_api.py
│  │  └─ tests/
│  └─ schemas/
│     ├─ blueprint_ir.schema.json
│     ├─ blueprint_manifest.schema.json
│     ├─ graph_summary.schema.json
│     └─ tool_response.schema.json
│
├─ examples/
│  ├─ commands/
│  ├─ sample_outputs/
│  └─ sample_queries/
│
├─ tests/
│  ├─ fixtures/
│  ├─ ir_validation/
│  ├─ slicing/
│  └─ readonly_safety/
│
└─ scripts/
   ├─ dump_blueprint.bat
   ├─ dump_blueprint.ps1
   ├─ dump_project_blueprints.ps1
   └─ build_index.py
```

你需要根据实际需求判断哪些目录必要，哪些目录可以后续再加。不要为了形式复杂而保留不必要模块。

---

# Blueprint IR设计要求

你需要设计统一的Blueprint IR。IR需要至少表达：

## 资产级信息

- asset_path
- package_path
- asset_name
- blueprint_class
- generated_class
- parent_class
- blueprint_type
- engine_version
- plugin_dependencies
- parse_time
- parse_status
- warnings
- errors

## 成员信息

- variables
- functions
- macros
- event_dispatchers
- interfaces
- implemented_interfaces
- timelines
- components

## 图信息

- graph_id
- graph_name
- graph_type
- graph_owner
- node_count
- pin_count
- edge_count
- entry_nodes
- local_variables
- graph_warnings

## 节点信息

- node_id
- node_guid
- node_class
- node_title
- node_comment
- node_position
- node_enabled_state
- node_type_category
- function_reference
- variable_reference
- event_reference
- macro_reference
- pins
- metadata

## Pin信息

- pin_id
- pin_name
- direction
- category
- sub_category
- object_type
- container_type
- is_exec
- is_data
- default_value
- default_object
- linked_to

## 边信息

需要区分：

- 执行流边 exec edge
- 数据流边 data edge
- delegate edge
- latent continuation edge，如果无法准确支持，需要标记为unknown或partial

每条边至少包含：

- edge_id
- from_node_id
- from_pin_id
- to_node_id
- to_pin_id
- edge_kind
- type_info

---

# 大蓝图处理要求

部分蓝图文件非常大，不能假设Agent一次可以读取完整IR。

你需要设计分层输出和切片机制。

至少包括：

## 1. Manifest层

用于让Agent快速理解资产总体结构：

- 资产路径
- 父类
- 蓝图类型
- 图列表
- 每个图节点数
- 变量列表
- 函数列表
- 事件入口列表
- 组件列表
- 复杂度统计

## 2. Graph Summary层

用于让Agent理解某个图的概要：

- 图类型
- 入口节点
- 关键执行链摘要
- 调用的函数
- 读写的变量
- 外部对象引用
- 节点类型分布
- 潜在复杂区域

## 3. Node Detail层

用于Agent按需读取具体节点：

- 节点完整信息
- Pin完整信息
- 上游节点
- 下游节点
- 所在局部子图

## 4. Slice层

支持按照以下方式切片：

- 从某个事件入口沿执行流向后追踪
- 从某个节点向前追踪依赖
- 从某个变量追踪读写位置
- 从某个函数追踪调用位置
- 从某个UI控件追踪绑定事件
- 从某个输入事件追踪后续行为

切片输出必须是合法子图，不能只有碎片文本。子图中所有节点和边都要能回溯到原始IR。

---

# 正确性校验要求

你必须设计并实现校验机制。不能只依赖大模型判断解析是否正确。

至少包括：

## 1. 结构完整性校验

检查：

- node_id是否唯一
- pin_id是否唯一
- 所有edge的两端是否存在
- 所有linked_to是否能解析到真实Pin
- Pin方向是否匹配
- Exec Pin和Data Pin是否区分正确
- 图中节点数量是否和UE读取数量一致
- 变量、函数、宏是否能回溯到UBlueprint内部定义

## 2. 只读安全校验

检查：

- 是否写入了原始Content目录
- 是否调用了SavePackage
- 是否修改了Blueprint状态
- 是否标记了资产Dirty
- 是否覆盖了已有输出
- 是否在未指定overwrite时写入旧文件

## 3. 解析覆盖率校验

统计：

- 总蓝图数
- 成功解析数
- 部分解析数
- 失败解析数
- 各蓝图类型覆盖情况
- 常见失败原因
- unsupported node class统计
- unknown pin type统计

## 4. IR Schema校验

所有导出的JSON必须能通过schema校验。

---

# Agent调用要求

最终项目必须让后续Agent可以无障碍调用。

你需要产出工具说明文件，例如：

```text
agent_tools/blueprint_tool_manifest.json
```

其中列出每个工具：

- tool_name
- description
- input_schema
- output_schema
- command
- whether_requires_unreal_editor
- whether_uses_cache
- side_effect_level
- safety_notes

side_effect_level必须至少区分：

```text
read_only
writes_output_only
modifies_project_files
modifies_assets
```

当前阶段所有可用工具都必须是：

```text
read_only
```

或：

```text
writes_output_only
```

不能出现：

```text
modifies_project_files
modifies_assets
```

---

# 输出要求

你在执行任务时，需要按照以下顺序输出阶段性成果。

## 第一部分：项目理解与风险判断

说明你对任务的理解，尤其要明确：

- 为什么不能直接解析`.uasset`
- 为什么需要UE Editor C++插件或Commandlet
- 为什么当前阶段只做正向解析
- 哪些UE蓝图类型需要考虑差异
- 哪些行为可能破坏原始资产，必须禁止

## 第二部分：原子能力拆分

输出表格，包含：

- 原子能力名称
- 职责
- 输入
- 输出
- 是否必要
- 是否可独立测试
- 是否会写原始文件
- Agent如何调用

## 第三部分：推荐项目目录

给出完整项目目录，并解释每个目录和关键文件的作用。

要求目录结构不要臃肿，必须服务于当前阶段目标。

## 第四部分：正向解析流程设计

描述从命令行输入到IR输出的完整流程。

必须包含：

- 单蓝图解析流程
- 全项目批量解析流程
- 大蓝图分层解析流程
- 失败处理流程
- 缓存复用流程

## 第五部分：Blueprint IR Schema草案

给出核心JSON结构草案。

不要求一开始覆盖所有字段，但必须能表达：

- 资产
- 图
- 节点
- Pin
- 边
- 变量
- 函数
- 组件
- 警告和错误

## 第六部分：Agent工具接口设计

给出Agent可调用工具列表，包括：

- 工具名
- 作用
- 输入
- 输出
- 是否需要启动UE
- 是否读取缓存
- 典型调用场景

## 第七部分：只读安全方案

明确说明如何保证不破坏原始UE工程，包括：

- 输出目录隔离
- 禁止保存资产
- 禁止修改Content
- overwrite控制
- dry-run模式
- 日志记录
- Dirty状态检测

## 第八部分：测试方案

至少包含：

- 单元测试
- IR Schema测试
- 只读安全测试
- 多蓝图类型测试
- 大蓝图切片测试
- 批量解析测试
- 回归测试

## 第九部分：最小可行版本MVP计划

给出MVP阶段的实现范围。

MVP必须优先完成：

- 普通UBlueprint解析
- EventGraph解析
- FunctionGraph解析
- Node解析
- Pin解析
- Pin连接解析
- 变量解析
- 组件树解析
- JSON导出
- Schema校验
- 只读安全检查
- Agent工具文档

MVP可以暂缓：

- 反向生成蓝图
- 自动修复蓝图
- 蓝图编译修改
- AnimGraph完整语义分析
- Widget绑定事件的深层语义
- Timeline复杂语义
- 宏展开语义
- 跨蓝图调用全局分析

## 第十部分：下一阶段预留

说明当前目录和接口如何为下一阶段反向构建蓝图预留空间，但不能在当前阶段实现反向构建。

---

# 执行约束

你必须遵守：

1. 不要直接操作或破坏原始UE资产。
2. 不要把当前任务扩大成完整蓝图编辑器。
3. 不要实现反向蓝图生成。
4. 不要使用大模型猜测`.uasset`二进制结构。
5. 不要为了完整性设计过度复杂的目录。
6. 所有原子能力必须有明确必要性。
7. 所有输出必须服务于后续Agent调用。
8. 如果某个UE API不确定，必须标注为待验证，不要假装确定。
9. 如果某个蓝图类型当前无法完整支持，必须标注为partial support。
10. 所有生成文件必须进入独立输出目录。
11. 默认流程必须是只读。
12. 所有解析结果必须能回溯到UE内部对象或导出的IR字段。

---

# 最终交付目标

请最终交付一份完整设计方案，使我可以据此启动项目实现。

交付内容至少包括：

1. 原子能力拆分说明
2. 推荐项目目录
3. 正向解析流程
4. Blueprint IR Schema草案
5. Agent工具接口设计
6. 大蓝图切片机制
7. 只读安全机制
8. 正确性校验机制
9. MVP实现计划
10. 后续反向构建阶段预留设计

当前阶段的目标不是把所有功能一次性做完，而是建立一套正确、稳定、可扩展、可被Agent调用的蓝图解析基础设施。
