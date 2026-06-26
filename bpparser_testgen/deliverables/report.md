# 蓝图解析测试体系构建完成报告

面向 **UE 5.4**，目标工程 `E:\BPTestProject\BPTest`，资产统一位于 `/Game/BPParserTest/`。

---

## ✅ 本地验收结果（已在 UE 5.4.4 实测，非推断）

| 项 | 结果 |
|---|---|
| 引擎版本 | UE **5.4.4** (35576357) |
| 插件编译 | **通过**（BPParserTestGen + BlueprintAgentTools 均编译成功） |
| 生成执行 | `-run=BPParserTestGen` 成功，写出 `generation_log.json` |
| 资产生成 | **17/17 全部创建并保存**（`Content/BPParserTest/*.uasset` 已落盘） |
| 蓝图编译 | **failed=0**：13 个 up_to_date + 2 个 warnings（BP_BPParserTestComponent、BP_05 的 ReturnNode exec 警告，非阻断）；Enum/Struct 为 n/a |
| 前置条件 | `BPTest` 原为纯蓝图工程，已加入最小 C++ 模块 + 两个 Target.cs 转为代码工程（见 §8/§10），并在 `.uproject` 启用两个插件 |

> 编译期/运行期发现并修复的 UE 5.4 真实问题（均已提交）：ExecutionSequence 引脚 API、SpawnActorFromClass 类型、FStructVariableDescription 头文件、SwitchEnum.Enum 直接赋值、SpawnActor 的 PostPlacedNewNode/AllocateDefaultPins 顺序、Enum 工厂改用 FactoryCreateNew、Array 容器节点改用 UK2Node_CallArrayFunction、CreateDelegate 连线后再绑定函数、SpawnTransform by-ref 引脚接 MakeTransform。
> 仍需人工在编辑器内目视确认的细节（compile 通过但不等于语义 100% 对）：委托 Bind 红线、SwitchEnum 各 case 引脚、宏内部展开、BP_05 宏体（手动）、Soft 引用默认值、Timeline/Async（未自动生成）。

---

## 0. 第一阶段：环境与能力检查（结论）

| 项 | 结论 | 依据 |
|---|---|---|
| UE 版本 | **5.4** | `BPTest.uproject` 的 `EngineAssociation":"5.4"` |
| 当前工程 | `E:\BPTestProject\BPTest`（纯蓝图模板，无 C++ Source 模块） | `.uproject` 仅启用 ModelingToolsEditorMode |
| 允许执行 UE Python | **未知/未依赖**：纯 Python 无法可靠建 K2 节点，故不采用 | 默认值=不依赖 Python |
| 允许 Editor Utility BP | 可，但同样无法建节点连线，故不采用 | — |
| 允许 C++ Editor 插件 | **采用此方案**（能力最全） | 同生态已有 C++ 插件 BlueprintAgentTools |
| 常见插件 | 仅 ModelingToolsEditorMode；**Enhanced Input/UMG/GameplayTags/AIModule 未确认启用** | 仅依赖 Engine 原生模块，不依赖可选插件 |
| 本工具作者环境 | **无法编译/运行 UE，无法出 PNG/SVG** | shell 在该环境不可用 |

> 因此：生成路径 = **C++ Editor 插件 + Commandlet/菜单**；可视化 = **DOT + Mermaid 源码 + 渲染脚本**（PNG/SVG 待本地生成）。

---

## 1. 本次生成或修改的内容
- 新建 UE 5.4 C++ Editor 插件 **BPParserTestGen**（15 个源文件），可在 UE 内通过菜单 / 控制台 / Commandlet 程序化生成全部测试资产，并自动 Compile + Save，输出 `generation_log.json`。
- 设计并构建 **5 个支持资产 + 11 个测试蓝图 + 1 个负面用例蓝图**（共 17 个）的生成逻辑。
- 为 17 个资产编写**预期解析 JSON**（与 BPAT IR schema 对齐）。
- 为 12 个测试蓝图编写 **DOT + Mermaid** 结构图，并提供渲染脚本。
- 本地验收前审计：精简 Build.cs 依赖（移除未用/已弃用模块降低编译面）；新增 **BP_11_SupplementalCoverage** 折叠原“部分覆盖”项；新增本地验收脚本与验收门槛文档。
- 编写**覆盖矩阵**、**人工检查指南**、**回归协议**、**README**、本**报告**。
- 自检修复：补齐 builders 缺失的 K2Node 头文件（编译级）；为 BP_04 实际加入 Switch-on-String 与 ForEachLoop 使其与注释一致，并同步 JSON/DOT/MMD。

## 2. 资产清单（`/Game/BPParserTest/`）

| 资产 | 类型 | 作用 |
|---|---|---|
| E_BPParserTestState | UserDefinedEnum | Idle/Moving/Attacking/Dead |
| ST_BPParserTestData | UserDefinedStruct | 10 字段（int/text/float/vector/rotator/transform/array<name>/enum/actor ref/soft object） |
| BPI_BPParserTest | Blueprint Interface | GetParserTestName()->String、ReceiveParserTestData(Data)、CanAcceptState(State)->Bool |
| BP_BPParserTargetActor | Actor | 实现接口；含 TestRoot/TestMesh 组件；供 Spawn/Cast/Interface/组件引用 |
| BP_BPParserTestComponent | ActorComponent | ComponentTag/ActivationCount 变量 + IncrementActivation 函数 |
| BP_01_PrimitivePins_Basic | Actor | 基础数据 Pin、默认值、Make/Break、Print、纯/非纯 |
| BP_02_StructEnumContainers | Actor | Struct/Enum/Array/Set/Map/Switch/ForEach |
| BP_03_ObjectReference_Cast_Interface | Actor | Object/Actor/Class/Soft 引用、Cast、Interface、Self、Target、IsValid |
| BP_04_ExecFlow_Control | Actor | Sequence/Branch/DoOnce/FlipFlop/Gate/ForLoop/ForLoopWithBreak/ForEach/SwitchInt/SwitchString/Delay/Reroute/汇合 |
| BP_05_Functions_Macros_LocalVariables | Actor | 普通/纯/多输出函数、Local Var、Macro、多 Graph |
| BP_06_Delegates_EventDispatchers | Actor | Event Dispatcher、Bind/Call/Unbind、Custom Event、Delegate Pin |
| BP_07_Latent_Timeline_Async | Actor | Delay/Timer/Custom Event；Timeline/Async 标注为不可自动覆盖 |
| BP_08_ComplexGameplayLikeGraph | Actor | 6 区域综合：Init/Spawn/Validate/Process/Dispatch/Log |
| BP_09_NodeFormatting_Comments_Reroutes | Actor | 注释/Reroute/未连接/默认值/坐标布局 |
| BP_10_ParserRoundTrip_Master | Actor | 跨资产回归入口 |
| BP_11_SupplementalCoverage | Actor | 补充覆盖：容器 Make/Get/Find/Remove/Values、Vector4/Color/DateTime/Timespan、by-ref 参数、DoN/WhileLoop/ForEachLoopWithBreak |
| BP_99_NegativeOrEdgeCases | Actor | **故意不完整**（compiles with warnings） |

## 3. 文件清单

**插件源码**（`bpparser_testgen/Plugins/BPParserTestGen/`）：
`BPParserTestGen.uplugin`、`Source/BPParserTestGen/BPParserTestGen.Build.cs`、
`Public/{BPParserTestGenModule.h, BPParserTestGenCommandlet.h, BPGenOrchestrator.h, BPGen.h, BPGenSupportAssets.h, BPGenTestBlueprints.h}`、
`Private/{BPParserTestGenModule.cpp, BPParserTestGenCommandlet.cpp, BPGenOrchestrator.cpp, BPGen.cpp, BPGenSupportAssets.cpp, BPGenBuilders_A.cpp, BPGenBuilders_B.cpp, BPGenBuilders_C.cpp}`

**交付物**（`bpparser_testgen/deliverables/`）：
`expected_ir/*.json`（17）、`viz/*.dot`（12）、`viz/*.mmd`（12）、`render_viz.ps1`、`local_acceptance_gate.md`、`coverage_matrix.md`、`manual_check_guide.md`、`regression_protocol.md`、`report.md`；根目录 `README.md`。
本地验收脚本（`scripts/`）：`build_plugin.ps1`、`run_generate.ps1`、`render_viz.ps1`、`dump_ir_sample.ps1`、`local_acceptance_checklist.md`。

## 4. 覆盖矩阵摘要
详见 `coverage_matrix.md`。概括：
- **已覆盖**：核心 Exec/Data 连线、基础数据 Pin（含 int64/real-float/real-double）、Object/Actor/Class/Component/Interface/Self/Target 引用、Array/Set/Map 主操作、Make/Break Struct、Switch(Int/String/Enum)、Branch/Sequence/Reroute/汇合、Delay+Timer(latent)、函数/多输出/Local Var、Comment/默认值/未连接 Pin、全部 DOT/Mermaid/JSON。
- **需要人工确认**：Struct/Enum 创建 API、Event Dispatcher、Interface 实现、StandardMacros 宏实例解析、Pure 标记、委托 pin 连线、Soft 引用默认值、隐藏 WorldContext/Advanced pin，以及**“是否编译通过 / PNG 是否生成”**。
- **本轮新增（BP_11）已覆盖**：MakeArray/MakeSet/MakeMap、Array Get/RemoveItem、Set Remove、Map Find/Values/Remove、Vector4、FColor、By-Ref 参数。
- **部分覆盖（剩余）**：Set Members in Struct、Enum to String/Name（仍未编入）。
- **需要人工确认（BP_11 内）**：DoN/WhileLoop/ForEachLoopWithBreak（StandardMacros 宏）、DateTime/Timespan（结构需运行时解析）。
- **无法自动覆盖**：Timeline、Async Action、Collapsed Graph、Anim/Widget Graph。

## 5. 每个蓝图的作用 + 13 项要点

> 字段：作用 / 覆盖 Pin / 覆盖节点 / 覆盖连线 / 主流程 / 可编译 / 需人工 / 已知限制 / JSON / 可视化。
> “可编译”一栏均为 **待 UE 验证**（本环境无法编译）；以 `generation_log.json` 为准。

- **BP_01**：基础数据 Pin。Pin: bool/byte/int/int64/real-float/real-double/name/string/text/vector/vector2d/rotator/transform/linearcolor。节点: Event/VarGet/VarSet/Add/Multiply/MakeTransform/BreakTransform/Conv/Concat/Print。连线: exec+data。流程: BeginPlay→SetInt→SetTransform→Print。需人工: 高级 pin 解析。限制: vector2d/linearcolor 仅变量。JSON/viz: `BP_01_*`。
- **BP_02**：容器/结构/枚举。Pin: struct/enum/array/set/map。节点: MakeStruct/BreakStruct/SwitchEnum/Array_Add/Array_Length/ForEachLoop/Set_Add/Set_Contains/Map_Add/Map_Keys。连线: exec+data(wildcard 容器)。需人工: wildcard 类型推断、Struct/Enum 资产。限制: Set/Map in struct 未做。
- **BP_03**：引用/Cast/接口。Pin: object/actor/class/softobject/softclass/interface/self/target。节点: Spawn/Cast/Message/IsValid/Self/GetComponentByClass。需人工: soft 引用默认值。
- **BP_04**：执行流。节点: Sequence/Branch/DoOnce/FlipFlop/Gate/ForLoop/ForLoopWithBreak/ForEachLoop/SwitchInt/SwitchString/Delay/Reroute。连线: exec/latent/汇合。需人工: StandardMacros 宏解析、SwitchString 字面值。
- **BP_05**：函数/宏/本地变量。Graph: EventGraph+3 函数+1 宏。需人工: Pure 标记、**宏体为手动**。
- **BP_06**：委托。节点: CreateDelegate/AddDelegate/CallDelegate/ClearDelegate/CustomEvent。连线: delegate/exec/data。需人工: 委托 pin 解析（最易错）。
- **BP_07**：Latent/Timer。节点: Delay/SetTimer/ClearTimer/CustomEvent/Self。限制: **Timeline/Async 无法自动覆盖**（用 Timer 替代）。
- **BP_08**：复杂综合。6 区域；含 Cast+Interface+Dispatcher+Reroute 汇合。
- **BP_09**：布局/注释/未连接。含空注释、长 reroute 链、NodeComment、孤立节点、默认值 Pin。
- **BP_10**：跨资产回归入口。Spawn/Cast/Interface/Dispatcher/MakeStruct/SwitchEnum。限制: Get Class Defaults 节点未生成（用 Spawn+Cast 替代）。
- **BP_11**：补充覆盖。节点: MakeArray/MakeSet/MakeMap、Array_Get/Array_RemoveItem、Set_Remove、Map_Find/Map_Values/Map_Remove、Sequence、DoN/WhileLoop/ForEachLoopWithBreak。Pin: Vector4/Color(+DateTime/Timespan 条件)、by-ref(int&)。需人工: 三个宏 + DateTime/Timespan 结构解析。
- **BP_99**：**故意不完整**。悬空 reroute 输出、孤立 Branch（无 exec-in）、未用变量；保留 BeginPlay→Print 有效路径使其 compiles-with-warnings。

## 6. JSON 输出位置
`bpparser_testgen/deliverables/expected_ir/<asset>.json`（17 个）。格式见各文件，含 asset/graphs/nodes/pins/edges/comments/variables/functions/macros/event_dispatchers/interfaces/coverage_tags，与 BPAT IR schema 对齐。

## 7. 可视化图输出位置
- 源码：`bpparser_testgen/deliverables/viz/<BP>.dot` 与 `<BP>.mmd`（各 12 个）。
- 图片：**本环境未生成**。运行 `deliverables/render_viz.ps1` 在本地生成 PNG/SVG（建议输出到工程 `Saved/BPParserTestReports/`）。
- 视觉约定：实线=exec、虚线=data、加粗/点划=delegate/latent、标注 `cast object input` / `interface message target` / `object ref`，含图例。

## 8. UE 中如何执行和查看
见 `README.md`：复制插件→生成工程文件→编译→`Tools→BP Parser Test→Generate` 或控制台 `BPParserTest.Generate` 或 Commandlet `-run=BPParserTestGen`→查看 `/Game/BPParserTest/` 与 `Saved/BPParserTestReports/generation_log.json`。

## 9. 人工检查清单
见 `manual_check_guide.md`（含“最容易被解析错的 9 个点”优先级清单）。

## 10. 已知限制

**A. 编译/运行未验证（最重要）**
本工具作者环境无 UE / 无法编译 C++、无法运行、无法出图。因此：
- 所有蓝图“是否编译通过”均标 **待 UE 验证**；以 `generation_log.json` 的 `compile_status` 为准。
- PNG/SVG 未生成，仅提供 DOT/MMD + 渲染脚本。
- 未伪造任何 `.uasset`、未伪造编译/图片/覆盖结果。

**B. UE 5.4 生成 API 需现场确认**（若签名不符需在 BPGen.cpp 微调，已集中于单文件、且每处失败仅记 warning 不中断）：
`FStructureEditorUtils::CreateUserDefinedStruct`、`UEnumFactory`+`FEnumEditorUtils`、`ImplementNewInterface(FTopLevelAssetPath)`、`AddEventDispatcher`（多播委托+签名图）、`UK2Node_FunctionEntry::AddExtraFlags(FUNC_BlueprintPure)`、`UPackage::SavePackage` bool 重载、StandardMacros 路径与宏名、CreateDelegate↔AddDelegate 委托 pin 解析。

**C. 依赖插件的内容**：本测试**不依赖**可选插件（Enhanced Input/UMG/GameplayTags/AIModule 等）；未把任何插件节点当核心覆盖。

**D. 无法自动覆盖**：Timeline、Async Action、Collapsed Graph、Anim/Widget Graph（见覆盖矩阵）。

**E. 跨版本差异**：float→double 统一、`FTopLevelAssetPath`、Latent metadata、SavePackage 签名等在 5.0~5.3/5.5 可能略有差异；本套面向 5.4。

**F. 预期 JSON 的 ID**：`node_id/pin_id` 为设计 ID，解析器用稳定 hash；回归请按结构匹配（见 `regression_protocol.md`）。

## 11. 后续我修改蓝图后如何提交给你做解析回归
见 `regression_protocol.md`：保存修改→用 BPAT `-run=BPATDump` 导出真实 IR（或发改动说明+截图）→我对比 `expected_ir` 基线→输出差异 JSON（added/removed/modified nodes·edges·pins·variables + risk_notes）→重生成可视化。
