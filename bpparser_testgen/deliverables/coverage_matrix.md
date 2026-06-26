# 覆盖矩阵 — BPParserTest 测试体系

状态取值（仅四种）：**已覆盖** / **部分覆盖** / **无法自动覆盖** / **需要人工确认**

> 全局前提（重要）：本环境**无法编译/运行 UE**，因此所有“已覆盖”表示**生成器已包含该项的构建逻辑**（节点/Pin/连线会被创建）。
> “是否编译通过 / .uasset 是否成功生成”必须在 UE 5.4 实际运行 `BPParserTest.Generate` 后由 `Saved/BPParserTestReports/generation_log.json` 与编辑器确认 —— 见 report.md「已知限制」。
> 凡是生成 API 在 UE 5.4 上有签名风险、或节点连线靠引擎自动解析的项，单独标注为 **需要人工确认**。

---

## 1. 执行流连线 / 控制流节点

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| Exec 输入/输出 | 连线 | BP_01/04/08 | 多数节点 | execute / then | 已覆盖 | 所有 impure 节点 |
| Event→函数调用 | 连线 | BP_01 | Event BeginPlay → PrintString | then→execute | 已覆盖 | |
| Branch | 节点 | BP_04 / BP_08 | K2Node_IfThenElse | Condition/Then/Else | 已覆盖 | BP_08 由 IsValid 驱动 |
| Sequence | 节点 | BP_04 / BP_08 | K2Node_ExecutionSequence | Then 0..N | 已覆盖 | BP_04 五路 |
| DoOnce | 节点 | BP_04 | K2Node_MacroInstance(DoOnce) | Completed | 需要人工确认 | StandardMacros 宏名解析 |
| DoN | 节点 | BP_11_SupplementalCoverage | K2Node_MacroInstance(DoN) | N/Exit | 需要人工确认 | StandardMacros 宏，需 UE 确认解析 |
| FlipFlop | 节点 | BP_04 | K2Node_MacroInstance(FlipFlop) | A/B | 需要人工确认 | 同上 |
| Gate | 节点 | BP_04 | K2Node_MacroInstance(Gate) | Enter/Exit | 需要人工确认 | 多个 exec 输入，连 Enter |
| ForLoop | 节点 | BP_04 | K2Node_MacroInstance(ForLoop) | First/Last/Loop Body/Index/Completed | 需要人工确认 | First=0/Last=3；Index→Print 经 To String 自动转换（已 IR 验证） |
| ForLoopWithBreak | 节点 | BP_04 | K2Node_MacroInstance(ForLoopWithBreak) | Loop Body/Break/Completed | 需要人工确认 | |
| WhileLoop | 节点 | BP_11 | K2Node_MacroInstance(WhileLoop) | Condition/Loop Body/Completed | 需要人工确认 | 条件默认 false（防死循环）；需 UE 确认 |
| ForEachLoop | 节点 | BP_02 / BP_04 / BP_08 | K2Node_MacroInstance(ForEachLoop) | Array/Loop Body/Array Element/Completed | 需要人工确认 | Array Element→Print 经 To String 自动转换（已 IR 验证） |
| ForEachLoopWithBreak | 节点 | BP_11 | K2Node_MacroInstance(ForEachLoopWithBreak) | Array/Loop Body/Array Element/Break/Completed | 需要人工确认 | Array Element→Print 经 To String 自动转换；StandardMacros 宏需 UE 确认 |
| Switch on Int | 节点 | BP_04 | K2Node_SwitchInteger | Selection/Default/case | 已覆盖 | case pin 由 AddPinToSwitchNode 生成 |
| Switch on String | 节点 | BP_04 | K2Node_SwitchString | Selection/Default/case | 需要人工确认 | case 字面值为引擎默认名 |
| Switch on Enum | 节点 | BP_02 / BP_08 / BP_10 | K2Node_SwitchEnum | Selection/Idle.. | 已覆盖 | |
| Delay（Latent） | 节点 | BP_04 / BP_07 | K2Node_CallFunction(Delay) | exec→then(latent) | 已覆盖 | UFunction Latent metadata |
| Set/Clear Timer | 节点 | BP_07 | K2_SetTimer / K2_ClearTimer | Object/FunctionName/Time | 已覆盖 | 名称按函数名调用 OnTimerTick |
| Timeline | 节点 | — | — | — | 无法自动覆盖 | UK2Node_Timeline 自动建模板风险高；用 Timer 替代，手动添加见指南 |
| Async Action | 节点 | — | — | — | 无法自动覆盖 | 无保证可用的引擎原生 async 节点 |
| Completed/Then/Loop Body/Break/Finished 等特殊 exec 输出 | Pin | BP_04 | 各宏 | Completed/Loop Body/Exit | 已覆盖 | |
| Reroute 中转 exec | 节点 | BP_04 / BP_08 / BP_09 | K2Node_Knot | Input/Output | 已覆盖 | BP_09 长链双 reroute |
| 多 exec 汇合到同一节点 | 连线 | BP_04 / BP_08 | Branch True+False → 同一 Reroute | converge | 已覆盖 | |
| 隐式类型转换（Autocast 节点） | 节点 | BP_02 / BP_04 / BP_11 | K2Node_CallFunction(Conv_IntToString)「To String (Integer)」 | InInt/ReturnValue | 已覆盖 | int→string 由 Schema 自动插入（TryCreateConnection→CreateAutomaticConversionNodeAndConnections） |
| Data 连线（循环值→消费者） | 连线 | BP_02 / BP_04 / BP_11 | Index/Array Element → To String → PrintString.InString | data | 已覆盖 | 经 FBPGen::ConnectData 建立并校验链接 |

---

## 2. 基础数据 Pin

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| Boolean | Pin | BP_01/04 | Var TestBool / FlowBool | bool | 已覆盖 | |
| Byte | Pin | BP_01 | Var TestByte | byte | 已覆盖 | |
| Integer | Pin | BP_01 | Var TestInt / Add_IntInt | int | 已覆盖 | |
| Integer64 | Pin | BP_01 | Var TestInt64 | int64 | 已覆盖 | |
| Float（real/float） | Pin | BP_01 | Var TestFloat | real/float | 已覆盖 | |
| Double/Real（real/double） | Pin | BP_01 | Var TestDouble / Multiply_DoubleDouble | real/double | 已覆盖 | |
| Name | Pin | BP_01/02 | Var TestName / NameSet | name | 已覆盖 | |
| String | Pin | BP_01 | Concat/PrintString | string | 已覆盖 | |
| Text | Pin | BP_01/02 | Var TestText / Struct DisplayName | text | 已覆盖 | |
| Vector | Pin | BP_01 | Var TestVector / MakeTransform | struct(Vector) | 已覆盖 | |
| Vector2D | Pin | BP_01 | Var TestVector2D | struct(Vector2D) | 已覆盖 | 仅变量，未连线 |
| Vector4 | Pin | BP_11 | Var SupVector4 | struct(Vector4) | 已覆盖 | 仅变量 |
| Rotator | Pin | BP_01 | Var TestRotator / MakeTransform | struct(Rotator) | 已覆盖 | |
| Transform | Pin | BP_01 | Make/Break Transform | struct(Transform) | 已覆盖 | |
| Color (FColor) | Pin | BP_11 | Var SupColor | struct(Color) | 已覆盖 | 仅变量 |
| LinearColor | Pin | BP_01 | Var TestLinearColor | struct(LinearColor) | 已覆盖 | 仅变量 |
| DateTime | Pin | BP_11 | Var SupDateTime | struct(DateTime) | 需要人工确认 | 仅当 /Script/CoreUObject.DateTime 运行时解析到才生成 |
| Timespan | Pin | BP_11 | Var SupTimespan | struct(Timespan) | 需要人工确认 | 仅当 Timespan 结构运行时解析到才生成 |

---

## 3. 引用类 Pin

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| Object Reference | Pin | BP_03/08/10 | Var TargetActorRef/TargetRef | object | 已覆盖 | |
| Actor Reference | Pin | BP_03 | Var TargetActorRef | object(Actor) | 已覆盖 | |
| SceneComponent Reference | Pin | BP_03 / 支持Actor | Var SceneCompRef / SCS | object(SceneComponent) | 已覆盖 | |
| 自定义 Actor BP Reference | Pin | BP_03/08/10 | Cast/Spawn 目标 | object(BP_BPParserTargetActor_C) | 已覆盖 | |
| Class Reference | Pin | BP_03 | Var TargetClassRef | class | 已覆盖 | |
| Soft Object Reference | Pin | BP_03 / Struct.SoftMesh | Var SoftActorRef | softobject | 需要人工确认 | 默认对象为空，需手填 |
| Soft Class Reference | Pin | BP_03 | Var SoftClassRef | softclass | 需要人工确认 | 同上 |
| Interface Reference | Pin | BP_03/08/10 | Interface Message self | interface | 已覆盖 | |
| Self 引用 | Pin | BP_03/07 | K2Node_Self | self | 已覆盖 | |
| Target Pin | Pin | BP_03/08 | Cast 结果→Message.self | target | 已覆盖 | |
| World Context Pin | Pin | BP_01/04/07 | PrintString/Delay（隐藏 WorldContext） | object | 需要人工确认 | 引擎隐藏 pin，解析端确认是否暴露 |
| Asset 相关引用 | Pin | BP_03(SoftMesh in Struct) | Struct.SoftMesh | softobject(StaticMesh) | 部分覆盖 | 仅结构体字段 |

---

## 4. 容器 Pin

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| Array | 容器 | BP_02/04/08 | Var IntArray/FlowArray/DataList | array | 已覆盖 | |
| Set | 容器 | BP_02 | Var NameSet | set | 已覆盖 | |
| Map | 容器 | BP_02/08 | Var ScoreMap/ResultMap | map | 已覆盖 | |
| Array Add | 节点 | BP_02/08 | Array_Add | TargetArray/NewItem | 已覆盖 | wildcard 由容器变量解析 |
| Array Get | 节点 | BP_11 | Array_Get | TargetArray/Index/Item | 已覆盖 | |
| Array Length | 节点 | BP_02 | Array_Length | TargetArray/ReturnValue | 已覆盖 | |
| Array Remove | 节点 | BP_11 | Array_RemoveItem | TargetArray/Item | 已覆盖 | RemoveItem 变体 |
| Set Add | 节点 | BP_02 | Set_Add | TargetSet/NewItem | 已覆盖 | |
| Set Contains | 节点 | BP_02 | Set_Contains | TargetSet/ItemToFind | 已覆盖 | |
| Set Remove | 节点 | BP_11 | Set_Remove | TargetSet/Item | 已覆盖 | |
| Map Add | 节点 | BP_02 | Map_Add | TargetMap/Key/Value | 已覆盖 | |
| Map Find | 节点 | BP_11 | Map_Find | TargetMap/Key/Value | 已覆盖 | |
| Map Keys | 节点 | BP_02 | Map_Keys | TargetMap/Keys | 已覆盖 | |
| Map Values | 节点 | BP_11 | Map_Values | TargetMap/Values | 已覆盖 | |
| Map Remove | 节点 | BP_11 | Map_Remove | TargetMap/Key | 已覆盖 | |
| Make Array | 节点 | BP_11 | K2Node_MakeArray | [0..2]/Array | 已覆盖 | |
| Make Set | 节点 | BP_11 | K2Node_MakeSet | Set | 已覆盖 | |
| Make Map | 节点 | BP_11 | K2Node_MakeMap | Map | 已覆盖 | |
| Break Array / ForEach | 结构 | BP_02/04/08 | ForEachLoop | Array Element | 已覆盖 | |
| 嵌套 Struct 含 Array/Set/Map | 结构 | 支持Struct / BP_02 | ST_BPParserTestData.Tags(array<name>) | array in struct | 已覆盖 | Set/Map in struct 未做（部分覆盖） |

---

## 5. 结构体 / 枚举 / 自定义类型 Pin

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| 内置 Struct (Vector/Transform) | 类型 | BP_01 | Make/Break Transform | struct | 已覆盖 | |
| 自定义 Struct ST_BPParserTestData | 资产 | 支持资产 | UserDefinedStruct | 10 字段 | 需要人工确认 | `FStructureEditorUtils::CreateUserDefinedStruct` 等 API 需 UE 5.4 验证 |
| Make Struct | 节点 | BP_02/08/10 | K2Node_MakeStruct | 字段 pin | 已覆盖 | |
| Break Struct | 节点 | BP_02 | K2Node_BreakStruct | 字段 pin | 已覆盖 | |
| Set Members in Struct | 节点 | — | K2Node_SetFieldsInStruct | — | 部分覆盖 | 未生成；可手动加 |
| 自定义 Enum E_BPParserTestState | 资产 | 支持资产 | UserDefinedEnum | Idle/Moving/Attacking/Dead | 需要人工确认 | `UEnumFactory`+`FEnumEditorUtils` 需 UE 5.4 验证 |
| Switch on Enum | 节点 | BP_02/08/10 | K2Node_SwitchEnum | 各枚举 case | 已覆盖 | |
| Enum→String/Name | 节点 | — | — | — | 部分覆盖 | 未生成；引擎自动 Conv 节点可手动加 |

---

## 6. 委托 / 事件 / 接口

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| Event Dispatcher | 委托 | BP_06/08/10 | 成员多播委托 + 签名图 | OnParserTestTriggered(Message) | 需要人工确认 | `AddEventDispatcher` 逻辑镜像编辑器实现，需 UE 验证 |
| Bind Event to Dispatcher | 节点 | BP_06 | K2Node_AddDelegate | Delegate 输入 | 需要人工确认 | Create→Bind 委托 pin 解析需确认 |
| Create Event | 节点 | BP_06 | K2Node_CreateDelegate | OutputDelegate | 需要人工确认 | SetFunction(HandleParserTestTriggered) |
| Unbind / Unbind All | 节点 | BP_06 | K2Node_ClearDelegate | — | 已覆盖 | |
| Call Dispatcher | 节点 | BP_06/08/10 | K2Node_CallDelegate | Message/Summary/Result | 已覆盖 | |
| Custom Event | 节点 | BP_06/07 | K2Node_CustomEvent | 自定义参数 pin | 已覆盖 | |
| Delegate/Event Pin 连线 | 连线 | BP_06 | CreateDelegate→AddDelegate | delegate | 需要人工确认 | |
| Blueprint Interface 调用 | 节点 | BP_03/08/10 | K2Node_Message | interface self | 已覆盖 | |
| Interface Message 调用 | 节点 | BP_03/08/10 | K2Node_Message(GetParserTestName) | self/Name | 已覆盖 | |
| Cast To 节点 | 节点 | BP_03/08/10 | K2Node_DynamicCast | Object/As.../valid/invalid | 已覆盖 | |
| Is Valid 节点 | 节点 | BP_03/08 | KismetSystemLibrary.IsValid | Object/ReturnValue | 已覆盖 | 用函数版；宏版 IsValid 可手动加 |
| Event Override | 节点 | 全部 | Event BeginPlay (ReceiveBeginPlay) | then | 已覆盖 | |
| 接口实现 (ImplementInterface) | 资产 | 支持Actor | BP_BPParserTargetActor | implements BPI | 需要人工确认 | `ImplementNewInterface(FTopLevelAssetPath)` 需 UE 5.4 验证 |

---

## 7. 函数 / 宏 / 图结构

| 覆盖项 | 类型 | 所在蓝图 | 节点 | Pin/连线 | 状态 | 备注 |
|---|---|---|---|---|---|---|
| 普通 Function | 图 | BP_05 | ComputeScore | Entry/Result | 已覆盖 | |
| Pure Function | 图 | BP_05 | NormalizeScore | 无 exec | 需要人工确认 | `AddExtraFlags(FUNC_BlueprintPure)` 是否生效需确认 |
| Impure Function | 图 | BP_05 | ComputeScore | exec | 已覆盖 | |
| 有返回值的函数 | 图 | BP_05 | ComputeScore→Total | Result.Total | 已覆盖 | |
| 多输入参数函数 | 图 | BP_05 | ComputeStats(A,B) | Entry 多 pin | 已覆盖 | |
| 多输出参数函数 | 图 | BP_05 | ComputeStats→(Sum,Product) | Result 多 pin | 已覆盖 | |
| By Reference 参数 | Pin | BP_11 | AccumulateByRef Entry | InOutValue (int&) | 已覆盖 | bIsReference=true |
| Local Variable | 成员 | BP_05 | ComputeScore.TempSum | — | 已覆盖 | `AddLocalVariable` |
| Macro | 图 | BP_05 | Macro_LogWithPrefix | 输入/输出 tunnel | 需要人工确认 | 宏外壳已建；**宏体连线为手动步骤** |
| Collapsed Graph | 图 | — | — | — | 无法自动覆盖 | 折叠图需选区操作，不自动生成；手动 |
| Reroute Node | 节点 | BP_04/08/09/99 | K2Node_Knot | — | 已覆盖 | |
| Comment Box | 注释 | 全部 BP | UEdGraphNode_Comment | text/bounds | 已覆盖 | BP_09 含空注释 |
| Node 默认值 | Pin | BP_01/02/04 | Add.B=8 / MakeStruct.ID 等 | default_value | 已覆盖 | |
| 未连接但有默认值的 Pin | Pin | BP_01/02/09 | PrintString.Duration / Struct.DisplayName | is_connected=false | 已覆盖 | |
| 自动展开高级 Pin | Pin | BP_01 | PrintString 高级 pin | bPrintToScreen 等 | 需要人工确认 | AdvancedPinDisplay 是否被解析 |
| 节点/变量重命名后解析稳定性 | 回归 | 全部 | — | — | 需要人工确认 | 回归阶段验证（regression_protocol.md） |

---

## 8. Graph 类型 / 可视化 / JSON 字段

| 覆盖项 | 类型 | 所在蓝图 | 状态 | 备注 |
|---|---|---|---|---|
| EventGraph (ubergraph) | Graph | 全部 BP | 已覆盖 | |
| Function Graph | Graph | BP_05 / 支持component | 已覆盖 | |
| Macro Graph | Graph | BP_05 | 需要人工确认 | 宏体手动 |
| Delegate Signature Graph | Graph | BP_06/08/10 | 需要人工确认 | 随 dispatcher 生成 |
| Interface Function Graph | Graph | BPI_BPParserTest | 需要人工确认 | |
| Anim/Widget Graph | Graph | — | 无法自动覆盖 | 不在本测试范围（解析器另有 partial 支持） |
| DOT 可视化 | 可视化 | 全部 12 个测试 BP（BP_01..BP_11 + BP_99） | 已覆盖 | deliverables/viz/*.dot（12 个） |
| Mermaid 可视化 | 可视化 | 全部 12 个测试 BP | 已覆盖 | deliverables/viz/*.mmd（12 个） |
| PNG/SVG 图片 | 可视化 | — | 需要人工确认 | 本环境未出图；运行 render_viz.ps1 生成 |
| JSON: asset/graphs/nodes/pins/edges | JSON | 全部 17 资产 | 已覆盖 | deliverables/expected_ir/*.json（17 个） |
| JSON: variables/functions/macros/dispatchers/interfaces | JSON | 各对应 BP | 已覆盖 | |
| JSON: coverage_tags | JSON | 全部 | 已覆盖 | |
| 差异回归 JSON | JSON | 流程 | 已覆盖 | regression_protocol.md 定义格式 |

---

## 9. 状态统计（按生成器构建逻辑计）

- 已覆盖：核心 Pin / 连线 / 节点 / Graph / 容器 / 引用 / 委托主路径，及全部 DOT/Mermaid/JSON。
- 需要人工确认：UE 5.4 生成 API 有签名风险者（Struct/Enum 创建、Dispatcher、Interface 实现、宏实例解析、Pure 标记、委托 pin 解析、Soft 引用默认值、隐藏 WorldContext/Advanced pin），以及“是否编译通过 / PNG 是否生成”。
- 部分覆盖（仍未编入，剩余）：Set Members in Struct、Enum to String/Name。其余原“部分覆盖”项已在 **BP_11_SupplementalCoverage** 编入（MakeArray/Set/Map、Array Get/RemoveItem、Set Remove、Map Find/Values/Remove、Vector4、FColor、By-Ref 参数 → 已覆盖；DoN/WhileLoop/ForEachLoopWithBreak、DateTime/Timespan → 需要人工确认）。
- 无法自动覆盖：Timeline、Async Action、Collapsed Graph、Anim/Widget Graph。

> 本轮（本地验收前审计）新增 BP_11_SupplementalCoverage，并据真实源码重新校准上表状态。
