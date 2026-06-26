# 人工检查指南（在 UE 5.4 中打开后该看什么）

> 目的：帮助你在 UE 编辑器中逐个核对生成结果，并重点关注“最容易被解析器解析错”的位置。
> 先看 `Saved/BPParserTestReports/generation_log.json` 的 `compile_status` 与 `notes`，再逐蓝图打开。

## ⚠ 重新生成工作流（务必遵守，否则会看到“脏资产”）
- **生成/重新生成前必须关闭 UE 编辑器。** 编辑器会锁定插件 DLL（编译失败），更危险的是：编辑器里加载的是旧版资产，保存/自动保存时会把**旧内容覆盖回磁盘**，冲掉刚生成的正确资产——这正是之前 BP_02 出现 MakeStruct 孤立引脚 / 容器类型未确定的真正原因（你看到的是被编辑器回写的旧版）。
- 正确顺序：①关闭编辑器 → ②`run_generate.ps1`（默认先清空 `/Game/BPParserTest` 再生成）→ ③再打开编辑器查看。
- `run_generate.ps1` / `local_acceptance.ps1` 现在会**检测到编辑器在运行就报错拒绝**，并默认清空旧资产。
- 这些是**生成物**：不要“先在编辑器改再重生成”（会被覆盖）。要改请改生成器源码重生成，或把要保留的蓝图移出 `/Game/BPParserTest`。

## 0. 通用检查（每个蓝图）
1. 能否双击打开、能否 **Compile**（左上角编译按钮变绿）。
2. Comment Box 是否存在、文字是否正确（解析器需识别注释与 bounds）。
3. 关键 exec 链是否连通；有没有“悬空但本应连上的”连线。
4. `generation_log.json` 中该资产 `notes` 列出的“manual/确认”项是否需要补。

## 1. 支持资产
- **E_BPParserTestState**：4 个枚举值显示为 Idle/Moving/Attacking/Dead（底层名可能是 NewEnumerator0..3，**这是正常的**，解析器应读 display name）。
- **ST_BPParserTestData**：10 个字段、类型正确；尤其 `Tags`=Array<Name>、`State`=枚举、`SoftMesh`=Soft Object(StaticMesh)。
- **BPI_BPParserTest**：3 个函数签名正确（含返回值）。
- **BP_BPParserTargetActor**：Components 面板有 TestRoot/TestMesh；Class Settings → Interfaces 含 BPI_BPParserTest。
- **BP_BPParserTestComponent**：IncrementActivation 函数存在。

## 2. 各测试蓝图重点
- **BP_01**：14 个变量类型对不对（尤其 float=real/float 与 double=real/double 的区别、Int64、Vector2D、LinearColor）；`Add.B=8`、`Concat.A="TestInt="` 默认值在；PrintString 的 Duration/Color 等未连接但有默认。
- **BP_02**：Make/Break 自定义 Struct；Switch on Enum 出 4 个枚举 case；Array/Set/Map 三类 wildcard 容器函数的容器 Pin 类型是否被正确推断（**易错点**）。
- **BP_03**：SpawnActor 的 Class 默认指向 BP_BPParserTargetActor；Cast 的 Object 输入、valid/invalid exec；Interface Message 的 self（target）与返回 Name；Soft 引用默认值为空（**需要你手填一个软引用做完整测试**）。
- **BP_04**：11 类控制流。**最易错/最需确认**：DoOnce/FlipFlop/Gate/ForLoop/ForLoopWithBreak/ForEachLoop 来自 `/Engine StandardMacros`，确认每个宏实例都成功解析（不是红色未知节点）；Branch 的 True+False 经 Reroute **汇合**到同一 Print；Switch String 的 case 字面值是引擎默认名（可改）。
- **BP_05**：4 个图（EventGraph + 3 函数 + 1 宏）。确认 **NormalizeScore 是否为 Pure**（无 exec pin）；ComputeScore 有 Local Var TempSum；**Macro_LogWithPrefix 宏体是空的（仅 tunnel），需要你手动连 Concat+PrintString**。
- **BP_06**：**委托最易错**。确认 Create Event→Bind 的红色 delegate pin 是否真的连上、Bind 是否绑定 OnParserTestTriggered；Call/Clear 是否引用同一 dispatcher；CustomEvent 的 Message 参数是否传到 Print。
- **BP_07**：Delay→SetTimerByFunctionName(OnTimerTick,0.5,Loop)→ClearTimer；OnTimerTick 自定义事件存在。**Timeline 与 Async 故意未生成**（见报告），如需补 Timeline：右键图 → Add Timeline → 加一条 Float Track。
- **BP_08**：6 个区域注释（Init/Spawn/Validate/Process/Dispatch/Log）；含 Cast + Interface + Dispatcher + Reroute 汇合，结构最复杂，重点看整体连通性。
- **BP_09**：布局/注释/未连接测试。**故意存在**：空注释框、悬空 reroute 之外的孤立节点、默认值 Pin、远距离坐标、NodeComment 气泡。
- **BP_10**：跨资产入口（Spawn/Cast/Interface/Dispatcher/MakeStruct/SwitchEnum）。
- **BP_11**：补充覆盖。确认 MakeArray/Set/Map 三个容器构造节点；Array Get/RemoveItem、Set Remove、Map Find/Values/Remove 的容器 Pin 类型是否被正确推断；SupVector4/SupColor 变量类型；**SupDateTime/SupTimespan 仅当结构解析成功才会出现**（缺失属预期，已在 notes 标注）；AccumulateByRef 的 InOutValue 是否为**按引用**参数；DoN/WhileLoop/ForEachLoopWithBreak 三个宏是否解析成功（WhileLoop 条件默认 false，不会死循环）。
- **BP_99**：**故意不完整**（compiles with warnings）：悬空 reroute 输出、无 exec 输入的孤立 Branch、未用变量。**这些不是 bug。**

## 3. 解析器最容易解析错的点（优先回归验证）
1. **容器 wildcard Pin**（Array/Set/Map 库函数）的实际类型推断。
2. **委托/事件 pin**（CreateDelegate↔AddDelegate 的 delegate 连线、Call/Clear 的 DelegateReference）。
3. **Latent 边**（Delay/Timer 的 then 应标 `latent_continuation`）。
4. **Cast 的 valid/invalid exec** 与 **AsXxx 数据输出**、**Interface Message 的 self/target**。
5. **Reroute（Knot）** 的透传：解析器应把经 reroute 的连线还原为逻辑端点，或如实表达中转点（两种策略都要能稳定输出）。
6. **未连接但有默认值的 Pin**、**节点默认值 vs 连线值** 的区分。
7. **多 exec 汇合**到同一输入 exec 的表达。
8. **Pure 函数**（无 exec）、**多输出函数**、**Local Variable**、**Macro 图 vs Function 图 vs EventGraph** 的 graph_type 区分。
9. **枚举 display name vs 内部名**、**float(real/float) vs double(real/double)**。

## 4. 与预期 JSON 对照
- 打开 `deliverables/expected_ir/<asset>.json`，按 `node_class` + `node_name` + pin 类型核对（**不要**按 node_id/pin_id 字面值，那是设计 ID；真实解析用稳定 hash）。
- 不一致时：若是引擎自动加的隐藏/高级 pin、或宏内部展开节点，属正常差异，记录即可。
