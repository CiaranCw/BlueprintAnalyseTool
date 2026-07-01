# Cross-Version Blueprint Analysis Capability Report

## 1. 当前问题
单一 UE 5.4 插件不足以覆盖其他 UE / 魔改 UE 项目，原因：蓝图的完整 Graph/Node/Pin/Edge 存在于序列化的
`UEdGraph`/`UK2Node`/`UEdGraphPin` 对象中，其类型（以及资产父类、自定义/插件 K2 节点）由**目标工程的引擎+插件**
定义。实测：`AClient` 用**源码魔改引擎 `AEngine`（UE 5.4.4，EngineAssociation 为 GUID）**，其 `WBP_Settings_Graphics`
的父类是项目 C++ 类 `/Script/AClient.RGSettingsGraphicsWidget`，并依赖 DLSS/Streamline 等插件 —— 用我们的
标准 launcher UE 5.4 无法忠实加载。因此必须让 dumper 在目标工程自身的引擎上下文中运行，且该 dumper 必须是
可复制到任意工程的**通用只读插件/Commandlet**，而非一次性硬编码实现。

## 2. 最终策略（三层模式）
- **Mode 0 offline_asset_scan**：纯 PowerShell，不启动 UE、不编译、不改工程。产出：`.uproject`/EngineAssociation
  （版本或 GUID）→ 解析 UERoot、引擎版本+是否源码魔改、AssetPath 合法性、`.uasset` 包版本头。不承诺 Graph。
- **Mode 1 python_partial**：目标 UE 的 PythonScriptPlugin 只读反射（不编译、不装插件）。产出：资产类型、父类、
  生成类、接口、依赖（AssetRegistry）。不承诺完整 EdGraph（Python API 无法稳定遍历节点/Pin/连线）→ 恒为 partial。
- **Mode 2 native_full**：只读 C++ Commandlet（`-run=BPParserTestDump` / `FBPGenIRDumper`）在目标工程+其引擎中运行。
  产出完整 IR（Graph/Node/Pin/Edge + 变量/函数/宏/委托/接口；Widget/Anim/插件/未知节点保留不丢弃）。
- **auto**：offline 打底 → 可行且获授权则 native_full → 否则 python_partial → 再回退 offline；始终输出 manifest，绝不静默失败，`fallbacks_used` 记录回退路径。

## 3. AClient / AEngine 推荐路径
`native_full`（项目插件增量编译）：不改 AEngine 源码、不改蓝图资产；把 `BlueprintAgentTools/BPParserTestGen`
以**源码**形式复制到 `AClient/Plugins`，用 AEngine 增量编译 `AClientEditor`（游戏模块已编译，主要编译本插件+重链），
运行只读 Commandlet 得到完整 IR + 预览图；完成后可移除插件。若用户不允许编译/装插件（当前即此选择），
降级为 python_partial / offline，并明确说明拿不到完整 EdGraph。

## 4. 已新增或修改内容
- 脚本：`scripts/analyze_blueprint.ps1`（统一入口：auto/offline/python-partial/native-full + GUID 引擎解析 +
  回退链 + 统一 manifest/summary/score/dot/mmd/logs，全程无硬编码工程/引擎路径）；`scripts/bp_analyze.py`
  （只读 python_partial 提取器）；`scripts/install_project_plugin.ps1`、`scripts/build_project_plugin.ps1`。
- 文档：`docs/agent_call_contract.md`、`docs/engine_compatibility.md`、`docs/integration_guide.md`、
  `docs/fallback_modes.md`、本报告。
- 兼容层：版本敏感面集中在 `BPGenUECompat.h` + `BPGenIRDumper.cpp`（已在 UE 5.4.4 源码构建上验证），
  `engine_compatibility.md` 说明其承担 BPATCompat 职责并给出扩展点（避免重复/死代码）。

## 5. Agent 调用方式
```powershell
.\scripts\analyze_blueprint.ps1 `
  -UERoot "D:\Projects\AEngine" `
  -ProjectUProject "D:\Projects\AClient\AClient.uproject" `
  -AssetPath "/Game/Assets/Widget/Settings/WBP_Settings_Graphics" `
  -OutputDir "D:\Projects\AClient\Saved\BPParserAgentReports" `
  -Mode auto            # 或 offline | python-partial | native-full
```
其他 Agent 只读 `manifest.json` 即可判断 status/mode/输出位置/是否 partial/缺什么/下一步。

## 6. 输出协议
`<OutputDir>/<SanitizedAssetPath>/`：`manifest.json`(主入口)、`blueprint_ir.json`(native_full) 或
`partial_ir.json`、`summary.md`、`understanding_score.json`、`graphs/*.json`、`viz/blueprint.dot|.mmd`、
`logs/warnings.json|errors.json|*_run.txt`。字段规范见 `agent_call_contract.md`。

## 7. 降级策略
native_full 失败按原因分类写入 `errors.json`（UERoot 无效 / Cmd 不存在 / 项目打不开 / 插件未装 / 插件编译失败 /
模块编译失败 / 缺依赖插件 / AssetPath 不存在 / 蓝图加载失败 / 自定义节点缺失 / Commandlet 崩溃 / 无输出），
自动回退 python_partial → offline，并在 manifest 记录 `fallbacks_used`；始终产出 manifest。

## 8. 已验证结果
- **native_full（BPTest，标准 5.4）**：`BP_08` → status=success，graphs=3 nodes=20 pins=92 edges=19 vars=5 funcs=1，
  manifest/ir/summary/dot/mmd/score 齐全。
- **offline（BPTest & AClient）**：均 partial；AClient 正确 GUID→AEngine(5.4.4, custom)，读到 uasset ue5=1012。
- **python_partial（AClient 真实 WBP，经 AEngine，非侵入，91s，0 error/0 warning）**：
  asset_type=WidgetBlueprint，parent=`/Script/AClient.RGSettingsGraphicsWidget`，generated=`WBP_Settings_Graphics_C`，
  15 个依赖（`WBP_Setting_EnumItem/SlideItem/InlineTitleItem/Space` 等子控件 + `/Script/AClient` + DLSS/Streamline 库）。
  即：这是一个「图形设置」UI 控件，父类为项目 C++ 控件，由若干设置项子控件组成，引用 NVIDIA DLSS/Streamline。

> 注：AClient 的解析产物写在 `AClient/Saved/BPParserAgentReports/`（工程内、含项目内部资产路径），未纳入本仓库以免泄露其内部结构。

## 9. 当前仍需用户确认
- native_full 是否可用于 AClient：是否允许把只读插件（源码）放入 `AClient/Plugins` 并用 AEngine 增量编译
  `AClientEditor`（数分钟、有失败风险；完成后可移除）。当前你的选择是「暂不编译/改动」→ 已交付 offline + python_partial。
- 若要完整 EdGraph（节点/Pin/连线/预览图），需要上面的授权，或你在 AClient 里预装该插件。

## 10. 下一步
你提供任意真实 AssetPath 后，其他 Agent 只需：
```powershell
.\scripts\analyze_blueprint.ps1 -ProjectUProject <proj> -AssetPath <path> -Mode auto [-AllowPluginInstall -AllowBuild]
```
读 `manifest.json` 即可获得结构化结果与后续指引；需要完整图时加 native-full 相关授权开关。
