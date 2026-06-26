# 本地验收门槛

> 配合 `scripts/local_acceptance_checklist.md` 使用。判定一律以 UE 实跑输出与
> `Saved/BPParserTestReports/generation_log.json` 为准，不以报告措辞为准。

## 必须通过
1. 插件能在 UE 5.4 项目中编译（`build_plugin.ps1` 退出码 0）。
2. `BPParserTest.Generate`（菜单/控制台）或 `-run=BPParserTestGen` 能执行。
3. `/Game/BPParserTest/` 中生成 5 个支持资产 + BP_01..BP_11 + BP_99。
4. `generation_log.json` 存在且可解析。
5. BP_01 到 BP_11 能打开。
6. BP_99 能打开，且其警告符合预期（悬空 reroute / 孤立 Branch / 未用变量）。
7. expected_ir 文件数量（17）与资产数量对应。
8. DOT/Mermaid 文件存在（各 12）。
9. 覆盖矩阵状态与实际生成结果一致（已覆盖项确有对应生成逻辑）。

## 可以接受但需记录
1. Soft 引用（BP_03 SoftActorRef/SoftClassRef、Struct.SoftMesh）默认值为空。
2. WorldContext / Advanced Pin 是否暴露取决于解析器策略。
3. StandardMacros 宏（DoOnce/ForLoop/Gate/ForEach/DoN/WhileLoop/...）展开后内部节点与 expected_ir 顶层视图有差异。
4. PNG/SVG 需本地安装 Graphviz 或 Mermaid CLI 后由 `render_viz.ps1` 生成。
5. Timeline / Async Action / Collapsed Graph 仍为手动或无法自动覆盖。
6. BP_11 的 SupDateTime/SupTimespan、AccumulateByRef、DoN/WhileLoop/ForEachLoopWithBreak 为 needs-confirm（见其 notes）。
7. Pure 函数（NormalizeScore）若未被标为 Pure，仍可编译，记录即可。

## 不能接受
1. 插件无法加载。
2. Commandlet 或菜单入口不存在。
3. 大量蓝图生成失败（`generation_log.json` 中 `failed` 远大于 needs-confirm 数）。
4. expected_ir JSON 非法（应能 `ConvertFrom-Json`，本审计已校验 17/17 合法）。
5. 覆盖矩阵声称“已覆盖”但源码或资产无对应生成逻辑。
6. 报告声称“已编译通过”但没有 generation_log 或 UE 验证（本套始终标“待 UE 验证”）。
