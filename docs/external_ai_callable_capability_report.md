# External AI Callable Blueprint Agent Full Capability Report

## 1. 本轮目标
让 AClient / 其他 UE 工程里的 Claude Code / Cursor Agent 等，通过**统一 request.json** 调用本 Agent，对真实
UE Blueprint/WBP 进行理解、修改、创建，输出机器可读结果、预览图、摘要，失败时给清晰原因，并尽量跨 UE 版本/魔改 UE 可迁移。

## 2. 已实现能力（核查结论：analyze/edit/create/validate 均具备对应功能，已实测）
| 能力 | 状态 | 入口 / 实现 | 实测 |
|---|---|---|---|
| Analyze 理解 | ✅ | `analyze_blueprint.ps1`（3 模式）+ `-run=BPParserTestDump`(`FBPGenIRDumper`) | BPTest BP_08 native_full 成功；AClient WBP python_partial 成功 |
| Edit 修改 | ✅ | `edit_blueprint.ps1` + `-run=BPATEdit`(`FBPATEdit`，plan/apply/verify/diff/rollback，9+ 原子操作) | 自测 8/8；本轮 plan-only 经 request 成功 |
| Create 创建 | ✅（本轮新增） | `create_blueprint.ps1` + `-run=BPCreate`(`FBPCreate`，spec→资产/变量/组件/函数/委托/EventGraph) | 由 request.json 创建 `BP_AgentCreatedExample` 成功，写盘 37KB，编译保存 0 警告 |
| Validate | ✅ | `validate_outputs.ps1` | 已有 |
| 统一调用 | ✅（本轮新增） | `blueprint_agent.ps1`（读 request.json，按 task_type 路由，写 dispatch_manifest） | analyze/edit/create 三类经同一入口全部 success |
| 跨版本/魔改 UE | ✅ | EngineAssociation（版本/GUID）解析 + 源码插件接入 + 兼容层 `BPGenUECompat.h` | AClient GUID→AEngine(5.4.4 源码版) 正确解析 |
| 三层降级 | ✅ | offline / python_partial / native_full + auto 回退链 | 三模式均实测 |

## 3. 统一请求结构
`request.json`：`{schema_version, task_type(analyze|edit|create|validate), project{ue_root,uproject,output_dir,
engine_policy}, execution{mode,strict,read_only,create_backup,allow_destructive_edit,render_preview}, request{}}`。
详见 `docs/request_schemas.md`；示例见 `bpparser_testgen/deliverables/requests/example_{analyze,edit,create}.json`。

## 4. 统一输出结构
- 顶层：`<output_dir>/<task_type>/dispatch_manifest.json`（AI 首读，含 status/mode/sub_output）。
- 子工具详细产物：analyze/create=`manifest.json`+`(blueprint|partial|created)_ir.json`+`summary.md`+`viz/*`+`logs/*`；
  edit=`edit_plan.json/edit_result.json/diff_report.json/baseline_ir/modified_ir/viz/*`。
- IR/Node/Pin/Edge 统一 schema 见 `docs/blueprint_ir_schema.md`；未知节点保留真实类名/标题/Pin，不丢弃。

## 5. 外部 Agent 调用方式
```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\request.json"
# 或指定/覆盖： -UERoot -ProjectUProject -OutputDir
```
其他 AI 只读 `dispatch_manifest.json` → 拿到 status/mode/sub_output → 再读子 `manifest.json` 取 IR/摘要/预览。

## 6. Analyze 能力
真实加载资产 → IR（Graph/Node/Pin/Edge + 变量/函数/委托/组件/接口/依赖）+ summary.md + dot/mmd +
understanding_score。native_full 给完整图；python_partial 给类型/父类/接口/依赖（明确 partial）；offline 给版本/依赖/包信息。

## 7. Edit 能力
自然语言 intent → 结构化 operations → edit_plan（前置条件+回滚）→ dry-run/plan-only 预览 → apply →
compile → save-on-success/rollback-on-failure → redump → diff → edit_result。原子操作含 insert_node_between、
remove_node(+preserve_exec)、connect/disconnect_pins、set_pin_default_value、add_reroute_on_edge、add_variable 等；
选择器支持 exec_out_connected 消歧 ghost 事件。破坏性操作需 allow_destructive_edit。

## 8. Create 能力
结构化 spec → 建资产(Actor/Component/Interface) → 加变量/组件/函数签名/委托 → EventGraph 节点(event/
call_function/branch/sequence/variable_get|set/comment)+按 `local_id.Pin` 连边+默认值 → compile → save →
created_ir + 预览。overwrite_policy=fail_if_exists|create_unique_name|overwrite_if_allowed。函数体/组件 attach、
WBP/Anim 创建标记为 manual/unsupported（不伪装）。

## 9. 兼容策略
native_full 必须在**目标工程对应 UE**中运行（完整图的唯一可靠来源）；python_partial/offline 为 partial/fallback；
插件以**源码**接入项目、增量编译；版本差异集中在 `BPGenUECompat.h`+`BPGenIRDumper.cpp`。见 `docs/engine_compatibility.md`、`fallback_modes.md`。

## 10. 安全策略
Analyze 只读；Edit 有 baseline+plan+backup+rollback，破坏性需授权；Create 遵守 overwrite_policy、默认不覆盖；
所有写操作记录在 result/manifest；失败必有 manifest+errors。

## 11. 已新增或修改文件（本轮）
- 源码：`BPCreate.h/.cpp`、`BPCreateCommandlet.h/.cpp`（`-run=BPCreate`）。
- 脚本：`blueprint_agent.ps1`（统一入口）、`create_blueprint.ps1`、`run_dump.ps1`。
- 文档：`request_schemas.md`、`agent_create_contract.md`、本报告；示例 `deliverables/requests/example_*.json`。
- 复用既有：analyze_blueprint/edit_blueprint/install/build_project_plugin/validate_outputs + BPParserTestDump/BPATEdit 及各契约文档。

## 12. 当前仍需用户验证
- 对 **AClient 真实 WBP** 的 **native_full**（完整节点/Pin/连线/预览）仍需你授权：把只读插件（源码）放入
  AClient/Plugins 并用 AEngine 增量编译 AClientEditor（可事后移除）。你此前选择「暂不编译/改动」→ 已交付 python_partial/offline。
- create/edit 对真实工程为写操作，需在目标工程执行并由你确认。

## 13. 是否达到外部 AI 可调用状态
**status: ready（对本地/BPTest 与非侵入模式已就绪并实测；对 AClient 的 native_full 待你授权编译）**
- 其他 AI 可用 `request.json` + `blueprint_agent.ps1` 完成 analyze/edit/create/validate，全部产出 manifest；
- partial 不伪装 full；未知节点不丢弃；无授权不改用户资产；无 manifest 不结束；
- 唯一「blocked」项：AClient 完整图需要你许可插件增量编译（属环境授权，非能力缺失）。
