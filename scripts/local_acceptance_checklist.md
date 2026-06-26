# 本地验收清单（BPParserTest）

> 所有命令用占位变量；先设置：
> ```powershell
> $UE_ROOT = "C:\Program Files\Epic Games\UE_5.4"
> $PROJECT_UPROJECT = "E:\BPTestProject\BPTest\BPTest.uproject"
> $OUTPUT_DIR = "E:\bp_out\BPTest"   # 必须在工程目录之外
> ```

## 步骤

1. **放置插件**
   - 复制 `bpparser_testgen/Plugins/BPParserTestGen` 到 `<Project>/Plugins/`。
   - （回归用）复制 `ue_plugin/BlueprintAgentTools` 到 `<Project>/Plugins/`。
   - 通过标准：两个 `.uplugin` 出现在 `<Project>/Plugins/` 下。

2. **编译**
   - `.\build_plugin.ps1 -UE_ROOT $UE_ROOT -PROJECT_UPROJECT $PROJECT_UPROJECT`
   - 通过标准：退出码 0；编辑器可启动且插件已加载。
   - 失败定位：看 `<Project>/Saved/Logs/*.log`；多为 API 签名不符（见 report.md「已知限制 B」逐项核对 BPGen.cpp）。

3. **生成资产**
   - `.\run_generate.ps1 -UE_ROOT $UE_ROOT -PROJECT_UPROJECT $PROJECT_UPROJECT`
   - 通过标准：`Saved/BPParserTestReports/generation_log.json` 存在；`failed=0`（或仅个别 `notes` 标注的 needs-confirm）。
   - 失败定位：读 `generation_log.json` 每个资产的 `compile_status` 与 `notes`。

4. **打开核对**
   - Content Browser 打开 `/Game/BPParserTest/`，逐个打开 BP_01..BP_11、BP_99。
   - 通过标准：能打开；按 `deliverables/manual_check_guide.md` 核对；BP_99 仅出现“预期内警告”。

5. **出图（可选）**
   - `.\render_viz.ps1`
   - 通过标准：`deliverables/viz/` 出现 PNG/SVG；缺 Graphviz/Mermaid 时脚本会明确提示（退出码 2），不会假装成功。

6. **回归抽样（可选）**
   - `.\dump_ir_sample.ps1 -UE_ROOT $UE_ROOT -PROJECT_UPROJECT $PROJECT_UPROJECT -OUTPUT_DIR $OUTPUT_DIR`
   - 通过标准：`$OUTPUT_DIR` 下出现 manifest.json + graphs/；与 `deliverables/expected_ir/BP_01_*.json` 结构对照。

## 通过标准（汇总）
见 `deliverables/local_acceptance_gate.md`。

## 失败时如何定位
- 编译错误 → `Saved/Logs`，对照 report.md「已知限制 B」高风险 API 清单。
- 节点未生成/红节点 → `generation_log.json` 的 `notes`（多为 StandardMacros 宏名或函数名差异）。
- 容器/委托类型错 → 这是解析器要测的点，记录到回归（regression_protocol.md）。
