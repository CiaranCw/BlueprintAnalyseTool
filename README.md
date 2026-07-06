# Blueprint Agent

一个**可被 AI 调用**的 Unreal Engine 蓝图工具：让 AI（Claude Code / Cursor / Codex 等）或人，能够**理解、修改、创建** UE 蓝图与 Widget 蓝图，并得到稳定的机器可读结果。

解析不走 `.uasset` 二进制猜测，而是**在蓝图自己的引擎里加载它**，经 UE 对象系统（`UEdGraph` / `UEdGraphNode` / `UEdGraphPin`）导出完整结构（Blueprint IR）。因此结果忠实于编辑器所见，且天然兼容项目里的自定义节点与插件。

> 目标引擎 **UE 5.4**，已在 **5.4.4（含源码/魔改引擎）** 实测跑通完整解析。

---

## 能做什么

| 任务 | 说明 | 产物 |
|---|---|---|
| **analyze** | 解析一个蓝图，导出完整 IR（图/节点/引脚/连线/变量/函数/宏/委托/接口/依赖） | `blueprint_ir.json` + summary + 可视化 |
| **edit** | 原子、可校验、可回滚地修改蓝图（插入/删除/改连线/改默认值/加变量…） | 备份 + `edit_plan` + `diff_report` |
| **create** | 从结构化 spec 创建新蓝图（Actor / Component / Interface） | `created_ir.json` |
| **status** | 只读探测：当前工程处于哪个阶段、能用哪些能力（不启动 UE） | `capability_state.json` |
| **warmup** | 一次性把只读插件装入并编译进目标工程，解锁完整解析（需授权、会构建） | `warmup_state.json` |
| **update** | 把已安装到某工程里的 Agent 幂等更新到最新版（自动备份、非破坏） | `update_result.json` |

除显式 `edit` / `create` 外，**绝不修改蓝图资产**；所有产物只写入工程外可指定的 `OutputDir`。

---

## 快速开始

前置：目标工程的 `.uproject`；对应 UE（可从 `EngineAssociation` 自动解析，或用 `-UERoot` 指定，支持源码/魔改引擎）。

**一个入口，一个 request.json：**

```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\request.json"
```

最简单的只读探测（不启动 UE，永远安全，建议新工程第一步）：

```json
{ "schema_version": "1.0", "task_type": "status", "project": { "uproject": "D:/Proj/MyGame.uproject" } }
```

解析一个蓝图（也可不用文件，直接传参）：

```powershell
.\scripts\blueprint_agent.ps1 -Task analyze -Mode auto `
  -ProjectUProject "D:/Proj/MyGame.uproject" -AssetPaths "/Game/UI/WBP_MainMenu"
```

结果只看一个文件：`<OutputDir>/<task>/dispatch_manifest.json`（`status` / `mode` / 指向详细 `manifest.json`）。详细结果里含 `blueprint_ir.json`、`summary.md`、`understanding_score.json`、`viz/blueprint.dot|.mmd`（若装了 Graphviz 另出 `.png/.svg`）、`logs/`。

请求字段完整规范见 [`docs/request_schemas.md`](docs/request_schemas.md)。

---

## 让项目里的 AI 自动识别并使用

把 Agent “安装”进目标工程，使其内置 AI 能自动发现并正确调用：

```powershell
.\scripts\install_agent_into_project.ps1 -TargetDir "D:/Proj" [-ProjectUProject "D:/Proj/MyGame.uproject"]
```

它会（幂等、非破坏地）写入：

- `AGENTS.md` / `CLAUDE.md` 中一段带标记的**受管块**（只替换标记块，不动你的其它内容）；
- `.cursor/rules/blueprint-agent.mdc`、`.claude/commands/blueprint.md`；
- `Tools/BlueprintAgent/`：脚本、文档、插件源码、机器可读描述符 `blueprint_agent.manifest.json`、示例请求、版本与同步基线。

安装后，项目里的 AI 应遵循的标准流程（这些已写进受管块，AI 会自动读到）：

1. **先探测**：跑 `task_type:"status"`，读 `capability_state.json`（`stage` / `capabilities` / `warmup_required`）。
2. **决定**：能力就绪 → 直接 `analyze` / `edit` / `create`（`execution.mode:"auto"`）；需要且用户允许构建 → 先 `warmup`；否则用 `python_partial` / `offline`（部分能力，绝不当作完整结果）。
3. **保持最新**：这是已安装副本，可能落后于源仓库。用 `check_project_agent_version.ps1` 检查，过期则 `update`（或 `task_type:"update"`）；若插件源码变了会标记 `needs_warmup_after_update`，重新 `warmup` 成功前不得声称完整解析可用。

跨工程/魔改引擎的安装与构建细节见 [`docs/integration_guide.md`](docs/integration_guide.md)；更新机制见 [`docs/update_sync_protocol.md`](docs/update_sync_protocol.md)。

---

## 执行模式

`-Mode auto`（默认）按可用性择优，并在 manifest 记录回退轨迹，绝不静默失败：

```text
editor_live  →  native_full  →  python_partial  →  offline_asset_scan
```

| 模式 | 是否启动 UE | 完整图结构 | 适用 |
|---|---|---|---|
| `editor_live` | 复用**已打开**的编辑器（文件队列，不新起进程） | 是 | 日常频繁解析/编辑，避免冷启动 |
| `native_full` | 启动 `UnrealEditor-Cmd` 跑 Commandlet | 是 | CI / 批处理 / 编辑器关闭时 |
| `python_partial` | 启动 UE + PythonScriptPlugin（只读反射） | 否（类型/父类/接口/依赖） | 未构建插件时的非侵入式部分解析 |
| `offline_asset_scan` | 否 | 否 | 工程/版本/资产路径/包头基线 |

完整图/节点/引脚/连线仅由 `native_full` 或 `editor_live` 保证。模式细节见 [`docs/fallback_modes.md`](docs/fallback_modes.md)、[`docs/editor_live_mode.md`](docs/editor_live_mode.md)。

---

## 安全边界

- `status` / `analyze` 严格只读：不保存、不编译、不改节点、不动资产。
- `edit`：默认非破坏，破坏性操作需 `allow_destructive_edit`；始终先备份并可回滚（Transaction + `diff_report`）。
- `create`：遵守 `overwrite_policy`，默认不覆盖已存在资产。
- `warmup`：会改工程（装插件 + 编译），仅在用户授权下进行；仍不修改任何蓝图资产。
- 编辑器打开时不热替换已加载的插件 DLL。

只读防护细节见 [`docs/readonly_safety.md`](docs/readonly_safety.md)。

---

## 仓库结构

```text
BlueprintAnalyseTool/
├─ scripts/                        # 所有入口：blueprint_agent.ps1（统一分发）+ 各专用脚本
├─ bpparser_testgen/
│  ├─ Plugins/BPParserTestGen/     # UE 5.4 C++ Editor 插件：IR 导出 / 原子编辑 / 创建 / 结构 diff / editor_live 服务
│  └─ deliverables/                # 系统化测试蓝图套件的预期 IR、可视化、覆盖矩阵
├─ docs/                           # 契约与指南（见下方索引）
├─ blueprint_agent.version.json    # 版本与 schema 版本（当前 0.4.0）
└─ AGENTS.md                       # 本仓库对编码 Agent 的工作规范
```

---

## 环境要求

- **UE 5.4**（含源码/魔改引擎）。构建插件需 Visual Studio 2022 + “使用 C++ 的游戏开发”工作负载。
- **Windows PowerShell 5.1+**（脚本入口）。
- 可选：**Graphviz**（`dot` 在 PATH 上）用于把可视化渲染成 PNG/SVG；没有则只出 DOT/Mermaid（不影响主流程）。
- 可选：目标工程的 **PythonScriptPlugin**（用于 `python_partial` 模式）。

---

## 文档索引

| 文档 | 内容 |
|---|---|
| [request_schemas.md](docs/request_schemas.md) | 统一 `request.json` 各任务字段 |
| [agent_call_contract.md](docs/agent_call_contract.md) | 解析（只读）调用契约 |
| [agent_edit_contract.md](docs/agent_edit_contract.md) | 原子编辑契约 |
| [agent_create_contract.md](docs/agent_create_contract.md) | 蓝图创建契约 |
| [fallback_modes.md](docs/fallback_modes.md) / [editor_live_mode.md](docs/editor_live_mode.md) | 四种模式与 editor_live |
| [warmup_and_capability_state.md](docs/warmup_and_capability_state.md) | warmup 与能力状态 |
| [update_sync_protocol.md](docs/update_sync_protocol.md) | 更新/同步机制 |
| [integration_guide.md](docs/integration_guide.md) / [engine_compatibility.md](docs/engine_compatibility.md) | 接入其它工程 / 引擎兼容 |
| [blueprint_ir_schema.md](docs/blueprint_ir_schema.md) | IR 字段语义 |
| [readonly_safety.md](docs/readonly_safety.md) | 只读安全防护 |

---

## 提交规范

不要 `git add .`。先 `git status` 看改动，只添加必要文件；不要提交缓存/构建产物（`Binaries/`、`Intermediate/`、`Saved/` 等已被 `.gitignore` 排除）。
