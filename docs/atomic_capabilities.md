# 原子能力清单

本阶段共 **21** 个原子能力。每个能力满足：必要、不冗余、规模合理、输入输出清晰、可独立测试、可被 Agent 无障碍调用。

---

## 1. 总表

| #  | 名称                              | 进程 / 语言 | 必要性 | 写原始文件 | 写 OutputDir | 是否需 UE |
| -- | --------------------------------- | ----------- | ------ | ---------- | ------------ | --------- |
| 1  | `BPATAssetEnumerator`             | UE C++      | 必要   | 否         | 否           | 是        |
| 2  | `BPATBlueprintLoader`             | UE C++      | 必要   | 否         | 否           | 是        |
| 3  | `BPATAssetInfoReader`             | UE C++      | 必要   | 否         | 否           | 是        |
| 4  | `BPATGraphEnumerator`             | UE C++      | 必要   | 否         | 否           | 是        |
| 5  | `BPATNodeReader`                  | UE C++      | 必要   | 否         | 否           | 是        |
| 6  | `BPATPinReader`                   | UE C++      | 必要   | 否         | 否           | 是        |
| 7  | `BPATEdgeResolver`                | UE C++      | 必要   | 否         | 否           | 是        |
| 8  | `BPATMemberReader`                | UE C++      | 必要   | 否         | 否           | 是        |
| 9  | `BPATComponentTreeReader`         | UE C++      | 必要   | 否         | 否           | 是        |
| 10 | `BPATWidgetTreeReader`            | UE C++      | 必要   | 否         | 否           | 是        |
| 11 | `BPATAnimGraphReader` (partial)   | UE C++      | 必要   | 否         | 否           | 是        |
| 12 | `BPATIRBuilder`                   | UE C++      | 必要   | 否         | 否           | 否¹       |
| 13 | `BPATIRValidator`                 | UE C++      | 必要   | 否         | 否           | 否¹       |
| 14 | `BPATIRSerializer`                | UE C++      | 必要   | 否         | **是**       | 否¹       |
| 15 | `BPATReadOnlyGuard`               | UE C++      | 必要   | 否         | 否           | 是        |
| 16 | `BPATOutputDirManager`            | UE C++      | 必要   | 否         | **是**       | 否¹       |
| 17 | `BPATDumpCommandlet`              | UE C++      | 必要   | 否         | **是**       | 是        |
| 18 | `bpat.indexer`                    | Python      | 必要   | 否         | **是**       | 否        |
| 19 | `bpat.slicer`                     | Python      | 必要   | 否         | 是²          | 否        |
| 20 | `bpat.query_api`                  | Python      | 必要   | 否         | 否           | 否³       |
| 21 | `bpat.schema_validator`           | Python      | 必要   | 否         | 否           | 否        |

> ¹ 此能力本身可独立运行，但当前阶段在 UE 编辑器进程中调用。
> ² 仅当显式 cache=on 时写入 `slices/`。
> ³ 默认不调 UE，仅当 IR 缺失且 `auto_dump=true` 时间接调用 `ue_runner`。

---

## 2. 详表

### 2.1 `BPATAssetEnumerator`

- **职责**：通过 `IAssetRegistry` 枚举 UBlueprint / UWidgetBlueprint / UAnimBlueprint 等资产路径与基础元信息。
- **输入**：`{ ProjectPath, ClassFilter[] }`
- **输出**：`TArray<FAssetEnumerationEntry { AssetPath, AssetClassPath, ParentClassPath }>`
- **依赖**：`AssetRegistry`
- **读取原始资产**：是（仅元数据，不加载对象）
- **修改原始资产**：否
- **测试**：mock `IAssetRegistry` 返回固定元数据集，断言过滤逻辑。
- **必要性**：批量解析的入口。
- **不合并**：枚举与加载是两个独立成本量级（前者 O(1) per asset，后者 O(N) per asset）。

### 2.2 `BPATBlueprintLoader`

- **职责**：按 PackagePath 只读加载 UBlueprint 及其子类。
- **输入**：`{ PackagePath, LoadFlags }`
- **输出**：`TWeakObjectPtr<UBlueprint>` + `FLoadStats { LoadedSubobjects, ElapsedMs }`
- **依赖**：`UObject` 子系统
- **读取原始资产**：是
- **修改原始资产**：否（绝不调 `Modify()` / `MarkPackageDirty()`）
- **测试**：编辑器测试加载固定 fixture .uasset，断言加载成功且包未脏。
- **必要性**：解析的前置条件。
- **不合并**：Loader 失败需要单独错误码（资产不存在 vs 加载异常 vs 编译失败 vs 类型不匹配）。

### 2.3 `BPATAssetInfoReader`

- **职责**：从 UBlueprint 提取资产级元数据。
- **输入**：`UBlueprint*`
- **输出**：`FBPATAssetInfo { AssetPath, AssetName, GeneratedClass, ParentClass, BlueprintType, EngineVersion, PluginDeps }`
- **测试**：fixture 蓝图 → 期望 JSON 比对。
- **不合并**：与 MemberReader 关注点不同（一个是身份信息，一个是成员集合）。

### 2.4 `BPATGraphEnumerator`

- **职责**：枚举 UBlueprint 内全部 `UEdGraph`：`UbergraphPages`、`FunctionGraphs`、`MacroGraphs`、`DelegateSignatureGraphs`、`IntermediateGeneratedGraphs`（仅记不解析）。
- **输入**：`UBlueprint*`
- **输出**：`TArray<FBPATGraphRef { GraphId, GraphName, GraphType, RawGraphPtr }>`
- **测试**：构造一个含 EventGraph + 自定义函数图 + 宏图的 fixture，断言计数与类型。
- **不合并**：Enumerator 决定"图的存在与归属"，Reader 决定"图内部"。

### 2.5 `BPATNodeReader`

- **职责**：读取单个 UEdGraph 内每个 `UEdGraphNode` 的结构属性（位置、注释、enable state、`NodeGuid`、节点类、引用对象）。
- **输入**：`UEdGraph*`
- **输出**：`TArray<FBPATNodeIR>`（不含 Pin 详情）
- **测试**：构造含已知节点子集的 fixture，断言节点类与字段非空。
- **不合并**：NodeReader 不关心 Pin 类型；PinReader 不关心节点位置。

### 2.6 `BPATPinReader`

- **职责**：读取节点的 `Pins` 数组，输出 PinIR（含 `PinType`、`DefaultValue`、`DefaultObject`、`bDefaultValueIsIgnored`、`AdvancedDisplay` 等）。
- **输入**：`UEdGraphNode*`
- **输出**：`TArray<FBPATPinIR>`
- **测试**：覆盖 `category=exec/object/struct/real/int/byte/string/wildcard`，每种至少 1 个 fixture pin。
- **不合并**：Pin 类型映射逻辑较多，单独便于在 schema 升级时只动一处。

### 2.7 `BPATEdgeResolver`

- **职责**：把 PinIR 上的 `LinkedTo` 集合解析成类型化 Edge（`exec` / `data` / `delegate` / `latent_continuation`）。
- **输入**：`{ NodeIR[], PinIR[] }`
- **输出**：`TArray<FBPATEdgeIR>` + warnings（悬空引用、跨图引用）
- **测试**：fixture 包含 1) exec 单连，2) data fan-out, 3) delegate, 4) 已知 latent 节点（如 `Delay`）的 Then 边。
- **不合并**：边解析是图论级聚合，错误模式（悬空、跨图、自环）与节点 / Pin 解析错误模式完全不同。

### 2.8 `BPATMemberReader`

- **职责**：读取 `NewVariables`（含 `FBPVariableDescription`）、`FunctionGraphs` 元信息、`MacroGraphs` 元信息、`DelegateSignatureGraphs`、实现的接口、`Timelines`。
- **输入**：`UBlueprint*`
- **输出**：`FBPATMemberSet { Variables, Functions, Macros, Dispatchers, Interfaces, Timelines }`
- **测试**：fixture 含每类成员 ≥ 1 个，断言计数与名字。

### 2.9 `BPATComponentTreeReader`

- **职责**：读取 `SimpleConstructionScript` 的 RootNodes + 子节点层级。
- **输入**：`UBlueprint*`（要求 `SimpleConstructionScript != nullptr`）
- **输出**：`FBPATComponentTree { Root, Children[] }`
- **测试**：fixture Actor BP 含三层组件，断言层级保留。

### 2.10 `BPATWidgetTreeReader`

- **职责**：读取 `UWidgetBlueprint::WidgetTree` 的层级与每个 `UWidget` 的类型 / 名字。
- **输入**：`UWidgetBlueprint*`
- **输出**：`FBPATWidgetTree`
- **支持级别**：full（结构）；绑定语义（`UPropertyBinding`）后置。
- **不合并**：与 SCS 不同（WidgetTree 是设计时层级；SCS 是运行时构造脚本）。

### 2.11 `BPATAnimGraphReader`（partial）

- **职责**：读取 `UAnimBlueprint::FunctionGraphs` 中 AnimGraph 顶层节点列表与连接，标记 `partial=true`。
- **输入**：`UAnimBlueprint*`
- **输出**：`FBPATAnimGraphIR { Nodes, Edges, partial=true, partial_reason }`
- **支持级别**：partial。状态机内部状态、转换条件不深度解析。
- **必要性**：AnimGraph 的节点继承 `UAnimGraphNode_Base`，与普通 UK2Node 类层级不同；通用 NodeReader 字段对它而言信息不全，必须有专用 reader 处理顶层。

### 2.12 `BPATIRBuilder`

- **职责**：把上述 reader 输出装配为完整的 `FBPATBlueprintIR`。纯内存构造，无 IO。
- **输入**：所有 reader 输出
- **输出**：`FBPATBlueprintIR`
- **测试**：mock 各 reader 输出 → 装配 → 比对完整 IR fixture。
- **不合并**：IRBuilder 与 IRSerializer 分离才能支持 `--dry-run`（构造但不落盘）。

### 2.13 `BPATIRValidator`

- **职责**：结构完整性校验（`node_id` 唯一、`pin_id` 唯一、edge 两端节点存在、Pin 方向匹配、exec/data 区分正确、计数一致）。
- **输入**：`FBPATBlueprintIR`
- **输出**：`FBPATValidationReport { errors[], warnings[] }`
- **测试**：故意制造每类违规的 fixture，断言报告对应 error。

### 2.14 `BPATIRSerializer`

- **职责**：分层落盘 IR：`manifest.json` / `graphs/<id>.summary.json` / `graphs/<id>.full.json` / `nodes/<id>.json`。
- **输入**：`{ IR, OutputLayout, OverwritePolicy }`
- **输出**：磁盘文件
- **测试**：在临时目录下序列化，断言文件存在 + 通过 schema 校验。

### 2.15 `BPATReadOnlyGuard`

- **职责**：注册 `UPackage::PackageMarkedDirtyEvent` 与 `UPackage::PackageSavedWithContextEvent`【待验证 V1】，对 `/Game/`、`/Engine/`、`/Script/` 包的脏与保存事件即刻报错并退出码 40。
- **输入**：编辑器钩子
- **输出**：违规报告 + 中止
- **测试**：单测中 mock `MarkPackageDirty(/Game/Foo)` 触发 → 期望 abort。

### 2.16 `BPATOutputDirManager`

- **职责**：校验 OutputDir 在 ProjectPath 外、不在 Engine 下；按 `OverwritePolicy` 规划目录布局；提供 `AssertWritable(Path)` 给所有写盘操作。
- **输入**：`{ OutputDir, ProjectPath, OverwritePolicy }`
- **输出**：`FBPATOutputLayout`
- **测试**：传入 OutputDir == Project 子目录 → 期望抛错。

### 2.17 `BPATDumpCommandlet`

- **职责**：编排 1-16，作为 `UnrealEditor-Cmd -run=BPATDump` 入口。
- **输入**：命令行
- **输出**：退出码 + IR 文件 + 日志
- **测试**：脚本级，调起一次 dump，断言退出码 0 + 关键文件存在。

### 2.18 `bpat.indexer`

- **职责**：基于 IR 文件构建索引：节点全文（搜节点标题 / 注释）、变量名 → 蓝图 / 节点列表、函数名 → 蓝图 / 节点列表。
- **实现**：SQLite + 一份 JSON 摘要。
- **输入**：`OutputDir`
- **输出**：`OutputDir/_index/`
- **测试**：纯 Python 单测，IR fixture 不依赖 UE。

### 2.19 `bpat.slicer`

- **职责**：基于 IR 做子图切片。算法：BFS / DFS 限深，过滤边类型（exec / data / 全部）。
- **输入**：`{ IR, start_node_id, direction(forward/backward/both), depth, edge_filter }`
- **输出**：合法子图 JSON（节点 + 边 + 边界注解）
- **测试**：fixture IR + 期望子图 JSON 比对。

### 2.20 `bpat.query_api`

- **职责**：暴露给 Agent 的高层只读查询函数（详见 `docs/agent_tools.md`）。
- **输入**：每个工具自有 schema
- **输出**：`tool_response.schema.json` 形式的统一外壳
- **测试**：每个工具至少一条响应快照测试。

### 2.21 `bpat.schema_validator`

- **职责**：用 `agent_tools/schemas/*.schema.json` 校验任意 IR 文件。
- **输入**：JSON 文件 + schema 名
- **输出**：`{ ok, errors[] }`
- **测试**：fixture 含合法 + 故意非法 各一份。

---

## 3. 不合并的边界总结

| A 与 B                              | 为什么不合并                                      |
| ----------------------------------- | ------------------------------------------------- |
| NodeReader vs PinReader             | 节点位置错误 vs Pin 类型错误的故障域不同         |
| PinReader vs EdgeResolver           | 单 Pin 信息 vs 多 Pin 拓扑信息                    |
| IRBuilder vs IRSerializer           | 内存装配 vs 磁盘 IO（dry-run 必须二者分离）       |
| ReadOnlyGuard vs OutputDirManager   | 编辑器副作用监控 vs 目标目录策略                  |
| query_api vs indexer                | 无状态查询 vs 持久化索引产物                      |
| AssetEnumerator vs BlueprintLoader  | O(1) 元信息扫描 vs O(N) 全对象加载，成本量级不同 |
| WidgetTreeReader vs ComponentTreeReader | 设计时 UI 树 vs 运行时 SCS 构造脚本             |

---

## 4. 集成调用顺序（单蓝图）

```text
[1] OutputDirManager.Validate(OutputDir, ProjectPath, OverwritePolicy)
[2] ReadOnlyGuard.Begin()
[3] AssetEnumerator.Lookup(AssetPath) → 验证存在 + 类型白名单
[4] BlueprintLoader.Load(PackagePath) → UBlueprint*
[5] AssetInfoReader.Read(BP)            ─┐
[6] MemberReader.Read(BP)                │
[7] ComponentTreeReader.Read(BP)         │  并行装配
[8] (类型分发) WidgetTreeReader / AnimGraphReader
[9] GraphEnumerator.Enumerate(BP)        │
[10] for each Graph:                     │
       NodeReader → PinReader → EdgeResolver
                                         │
[11] IRBuilder.Build(...)               ─┘
[12] IRValidator.Validate
[13] IRSerializer.WriteAll
[14] ReadOnlyGuard.End → 任意违规则退出码 40
[15] 写日志
```
