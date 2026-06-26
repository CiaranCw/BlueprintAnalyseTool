# BlueprintAnalyseTool

Unreal Engine 蓝图解析 Agent 底层工具集。

本项目目的是让上层 Agent **看懂** UE 蓝图：经由 UE 自身编辑器能力加载 `.uasset`，
解析出可靠的结构化中间表示（Blueprint IR），再让 Agent 基于 IR 做查询、检索、解释、推理。

> **当前阶段：仅正向解析（Blueprint → IR → Agent 查询）。**
> 反向构建（自然语言 / Spec → 蓝图 → `.uasset`）留给下一阶段，本阶段只做接口与字段层面的预留。

---

## 设计准则（不可让步）

1. **不直接解析 `.uasset` 二进制**。所有结构必须经由 UE 对象系统（`UEdGraph` / `UEdGraphNode` / `UEdGraphPin`）。
2. **整链路只读**。所有写入仅允许进入工程外的独立 `OutputDir`，严禁修改 `Content/`、`Source/`、`Config/`、原始 `.uasset`。
3. **可被 Agent 无障碍调用**。所有能力以命令行 + JSON 形式暴露，统一响应外壳。
4. **每个原子能力可独立测试**。复杂能力（如 Edge 解析、IR 构建）必须能脱离真 UE 工程跑。
5. **不确定的 UE API 必须标注待验证**。不允许"看起来差不多就用"。

详见 [`docs/readonly_safety.md`](docs/readonly_safety.md)。

---

## 目录速览

```text
BlueprintAnalyseTool/
├─ Requirement.md              原始需求
├─ README.md                   本文
├─ docs/                       设计文档（8 份）
├─ ue_plugin/                  UE C++ Editor 插件 + Commandlet
│  └─ BlueprintAgentTools/
├─ agent_tools/                Python Agent 工具（query_api / indexer / slicer）
├─ examples/                   命令行 / 输出 / 查询示例
├─ tests/                      跨语言测试 fixture
└─ scripts/                    Windows 批处理 / PowerShell 入口
```

---

## 快速开始（占位）

> 当前为骨架阶段，命令行尚未可执行；以下展示设计意图。

### 1. 解析单个蓝图

```powershell
UnrealEditor-Cmd.exe D:\MyGame\MyGame.uproject `
  -run=BPATDump `
  -AssetPath=/Game/Blueprints/BP_Hero `
  -OutputDir=D:\bp_out\MyGame `
  -OverwritePolicy=skip `
  -StrictReadOnly=1
```

### 2. 全工程批量解析

```powershell
UnrealEditor-Cmd.exe D:\MyGame\MyGame.uproject `
  -run=BPATDump `
  -ProjectScan=1 `
  -ClassFilter=Blueprint,WidgetBlueprint,AnimBlueprint `
  -OutputDir=D:\bp_out\MyGame
```

### 3. Agent Python 查询（不启动 UE）

```python
from blueprint_agent_tools.query_api import QueryAPI

api = QueryAPI(output_dir=r"D:\bp_out\MyGame")
api.list_blueprint_assets()
api.get_blueprint_manifest("/Game/Blueprints/BP_Hero")
api.trace_exec_flow("/Game/Blueprints/BP_Hero", start_node_id="node_0001", max_depth=8)
```

---

## 关键文档索引

| 文档                                               | 内容                                       |
| -------------------------------------------------- | ------------------------------------------ |
| [架构总览](docs/architecture.md)                   | 数据流、模块边界、调用顺序、API 决策       |
| [原子能力](docs/atomic_capabilities.md)            | 21 个原子能力的职责与边界                  |
| [Blueprint IR Schema](docs/blueprint_ir_schema.md) | IR 字段语义                                |
| [Agent 工具](docs/agent_tools.md)                  | 12 个对外工具、错误码、响应外壳             |
| [切片策略](docs/slicing_strategy.md)               | 大蓝图分层与子图切片                       |
| [校验策略](docs/validation_strategy.md)            | 结构 / 只读 / 覆盖率 / Schema 四类校验     |
| [只读安全](docs/readonly_safety.md)                | 七层防护与禁用 API 列表                    |
| [反向构建路线图](docs/roadmap_reverse_generation.md) | 反向阶段预留接口与沙箱机制               |
| [UE 5.4 现场 Spike](docs/spike_ue54.md)            | 8 项待验证 API 的现场验证脚本与通过标准    |

---

## 当前阶段支持矩阵

| 蓝图类型                    | 支持级别       | 备注                                |
| --------------------------- | -------------- | ----------------------------------- |
| `UBlueprint`（Actor/Pawn/Character/Component） | full           | EventGraph + FunctionGraph 全字段   |
| `UWidgetBlueprint`          | full（结构）  | WidgetTree 结构层；绑定语义后置     |
| `UAnimBlueprint`            | partial        | AnimGraph 顶层结构；状态机语义后置  |
| `UBlueprintInterface`       | full           |                                     |
| Macro Library / Function Library | full      |                                     |
| Data-only Blueprint         | full           | 只输出 manifest                     |
| `ULevelScriptBlueprint`     | partial        | 需 LoadMap，默认不在批量流程启用    |
| 其他 `UBlueprint` 子类      | partial        | 通用分支 + warning                  |

---

## 目标 UE 版本

UE **5.4**。其它版本兼容性留待后续 spike，详见 `docs/architecture.md` "待验证项"。

---

## 测试资产生成器（`bpparser_testgen/`）

用于验证解析 Agent 的**系统化蓝图测试资产生成器**（面向 UE 5.4）。

```text
bpparser_testgen/
├─ Plugins/BPParserTestGen/   UE 5.4 C++ Editor 插件，程序化生成 /Game/BPParserTest 测试蓝图
└─ deliverables/              预期解析 JSON / DOT+Mermaid 可视化 / 覆盖矩阵 / 报告
```

用法详见 `bpparser_testgen/README.md`。它会在测试工程中生成一套覆盖
Pin / 连线 / 节点 / Struct / Enum / 容器 / 委托 / 接口 / Cast / Latent / 注释 / Reroute
的测试蓝图，并附带每个蓝图的"预期解析结果"用于回归对比。

---

## 环境要求

- **UE 5.4**（C++ Editor 插件编译运行；需 Visual Studio 2022 + “使用 C++ 的游戏开发”工作负载）。
- **Python ≥ 3.10**：Agent 侧工具，依赖见 `agent_tools/pyproject.toml`。
  ```powershell
  cd agent_tools
  pip install -e .
  ```
- 无 Node 依赖；无需 Git LFS（仓库为纯文本源码/文档）。

---

## 未纳入版本库的内容

以下内容由 `.gitignore` 排除，**不会**进入仓库（属本地/生成产物）：

- UE 中间产物：`Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`、`.vs/`、生成的 `*.sln`/`*.vcxproj`。
- 构建产物：`build/`、`dist/`、`bin/`、`obj/` 及 `*.dll/*.lib/*.exe/*.pdb`。
- Python 缓存与虚拟环境：`__pycache__/`、`*.egg-info/`、`.venv/`、`.pytest_cache/` 等。
- 日志 / 临时 / 崩溃转储：`*.log`、`*.tmp`、`*.dmp`。
- 敏感信息：`.env`、密钥证书（`*.key/*.pem/*.pfx`）、`config.local.*`、`secrets.*` 等。
- IDE 个人配置：`.idea/`、`.vscode/*`（保留 `extensions.json` 与 `settings.example.json`）。

解析产物（IR / 索引）按设计只写到**工程外的 `OutputDir`**，本就不在仓库内。

---

## 贡献 / 提交流程

不要使用 `git add .`。建议：

```powershell
git status                 # 先看改动
git add <必要文件/目录>     # 按需添加，勿加缓存/产物
git commit -m "<说明>"
git push
```
