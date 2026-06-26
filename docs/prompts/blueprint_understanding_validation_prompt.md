# Blueprint Understanding Validation Prompt

你现在进入“已有蓝图理解能力验证模式”。

这个阶段的目标不是生成新的测试蓝图，而是验证当前 Blueprint Agent 是否能够正确读取、理解、解析、结构化表达、可视化展示一个已经存在的 Unreal Engine Blueprint 资产，并且能够作为可被其他 Agent 调用的工具稳定返回结果。

用户可能会提供一个蓝图资产路径，例如：

```text
/Game/Blueprints/BP_PlayerCharacter
/Game/BPParserTest/BP_04_ExecFlow_Control
/Game/UI/WBP_MainMenu
```

也可能是其他 Agent 在分析 UE 项目时调用你，要求你返回某个蓝图的结构、节点、Pin、连线、变量、函数、事件、组件、依赖关系或可视化图。

你的任务是：基于用户提供的 Blueprint Asset Path，对该蓝图进行真实解析，并输出人类可读和机器可读两类结果。

------

## 1. 总目标

给定一个已有 Blueprint 资产路径后，你需要完成：

1. 验证资产路径是否存在。
2. 判断资产类型：
   - Actor Blueprint
   - ActorComponent Blueprint
   - Widget Blueprint
   - Anim Blueprint
   - Blueprint Interface
   - Blueprint Macro Library
   - Function Library
   - 其他 Blueprint 派生资产
3. 读取蓝图基本信息。
4. 解析蓝图中的 Graph。
5. 解析每个 Graph 中的节点。
6. 解析每个节点的 Pin。
7. 解析节点间的边：
   - Exec 边
   - Data 边
   - Delegate/Event 边
   - Object Reference 边
   - Interface Message 边
   - Cast 输入边
   - Latent continuation 边
   - Reroute/Knot 边
8. 解析变量、函数、宏、事件、Dispatcher、Interface、组件、父类、实现接口、依赖资产。
9. 输出机器可读 JSON。
10. 输出人类可读摘要。
11. 输出 DOT / Mermaid 可视化。
12. 如工具链允许，渲染 PNG / SVG。
13. 输出调用状态、错误、警告和人工确认项。
14. 支持被 Claude Code CLI、Cursor Agent 或其他 Agent 调用。

------

## 2. 重要原则

请严格遵守：

1. 不要凭文件名猜测蓝图结构。
2. 不要伪造节点、Pin、连线或 Graph。
3. 不要把 expected_ir 当作真实解析结果。
4. 必须优先通过 UE Editor / Commandlet / 插件 API 读取真实蓝图资产。
5. 如果无法运行 UE 或无法加载资产，必须明确标记失败原因。
6. 如果某些节点、Pin、Graph 无法完整解析，必须标记为 `partial` 或 `manual_check_required`。
7. 不要只输出自然语言摘要，必须输出机器可读 JSON。
8. 不要只输出 JSON，也要输出人类可读解释和可视化。
9. 不要硬编码当前工程路径。
10. 不要只适配 `/Game/BPParserTest/`，要支持任意项目内 Blueprint Asset Path。
11. 解析输出必须稳定，便于其他 Agent 后续引用。

------

## 3. 输入参数

你应支持以下输入：

```text
AssetPath: /Game/...
ProjectUProject: <optional .uproject path>
UERoot: <optional UE installation root>
OutputDir: <optional output directory>
Mode: summary | full | json-only | viz-only | validate-callable
Strict: true | false
```

如果用户只提供 `AssetPath`，你需要从当前上下文、配置文件、脚本参数或项目根目录自动推断 `.uproject` 与输出目录。

如果无法推断，输出清晰错误，不要猜测本地绝对路径。

------

## 4. 推荐 CLI 调用形式

本 Agent 应提供或维护一个可被其他 Agent 调用的命令行入口，例如：

```powershell
.\scripts\analyze_blueprint.ps1 `
  -UERoot "<UE_ROOT>" `
  -ProjectUProject "<PROJECT_UPROJECT>" `
  -AssetPath "/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -OutputDir "<PROJECT>/Saved/BPParserAgentReports" `
  -Mode "full"
```

也应支持 Commandlet 调用，例如：

```powershell
& "<UE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "<PROJECT_UPROJECT>" `
  -run=BPATDump `
  -AssetPath="/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -OutputDir="<PROJECT>/Saved/BPParserAgentReports" `
  -unattended `
  -nop4
```

如果当前项目中这些脚本或 Commandlet 不存在，请补齐它们；如果已有但不完整，请修复到可被其他 Agent 调用的程度。

------

## 5. 输出目录规范

所有输出应放入：

```text
Saved/BPParserAgentReports/<SanitizedAssetPath>/
```

例如：

```text
Saved/BPParserAgentReports/Game_BPParserTest_BP_04_ExecFlow_Control/
```

至少输出：

```text
manifest.json
blueprint_ir.json
summary.md
graphs/
  EventGraph.json
  Function_*.json
  Macro_*.json
viz/
  blueprint.dot
  blueprint.mmd
  blueprint.png        # 如果可渲染
  blueprint.svg        # 如果可渲染
logs/
  analyze_log.txt
  warnings.json
  errors.json
```

------

## 6. 机器可读 Manifest

必须输出 `manifest.json`，格式建议如下：

```json
{
  "schema_version": "1.0",
  "status": "success",
  "asset_path": "/Game/BPParserTest/BP_04_ExecFlow_Control",
  "asset_name": "BP_04_ExecFlow_Control",
  "asset_type": "Blueprint",
  "blueprint_class": "Actor",
  "parent_class": "Actor",
  "ue_version": "5.4",
  "project_uproject": "",
  "generated_at": "",
  "outputs": {
    "ir": "blueprint_ir.json",
    "summary": "summary.md",
    "dot": "viz/blueprint.dot",
    "mermaid": "viz/blueprint.mmd",
    "png": "",
    "svg": ""
  },
  "counts": {
    "graphs": 0,
    "nodes": 0,
    "pins": 0,
    "edges": 0,
    "variables": 0,
    "functions": 0,
    "macros": 0,
    "dispatchers": 0,
    "components": 0
  },
  "warnings": [],
  "errors": [],
  "manual_check_required": []
}
```

状态只能使用：

```text
success
partial
failed
```

不要使用含糊状态。

------

## 7. Blueprint IR 输出格式

必须输出 `blueprint_ir.json`。

建议结构如下：

```json
{
  "schema_version": "1.0",
  "asset": {
    "asset_path": "/Game/BPParserTest/BP_04_ExecFlow_Control",
    "asset_name": "BP_04_ExecFlow_Control",
    "asset_type": "Blueprint",
    "generated_class": "",
    "parent_class": "",
    "implemented_interfaces": [],
    "dependencies": []
  },
  "blueprint": {
    "variables": [],
    "functions": [],
    "macros": [],
    "event_dispatchers": [],
    "components": [],
    "timelines": [],
    "graphs": []
  },
  "graphs": [
    {
      "graph_id": "",
      "graph_name": "EventGraph",
      "graph_type": "event",
      "nodes": [
        {
          "node_id": "",
          "node_guid": "",
          "node_name": "",
          "node_title": "",
          "node_class": "",
          "node_type": "",
          "position": {
            "x": 0,
            "y": 0
          },
          "comment": "",
          "enabled_state": "",
          "is_pure": false,
          "is_latent": false,
          "is_macro_instance": false,
          "macro_reference": "",
          "function_reference": "",
          "delegate_reference": "",
          "pins": [
            {
              "pin_id": "",
              "pin_name": "",
              "direction": "input",
              "category": "",
              "sub_category": "",
              "sub_category_object": "",
              "container_type": "none",
              "is_array": false,
              "is_set": false,
              "is_map": false,
              "is_reference": false,
              "is_const": false,
              "is_connected": false,
              "default_value": null,
              "default_object": null,
              "linked_to": []
            }
          ]
        }
      ],
      "edges": [
        {
          "edge_id": "",
          "from_node": "",
          "from_pin": "",
          "to_node": "",
          "to_pin": "",
          "edge_type": "exec",
          "through_reroute": false,
          "notes": []
        }
      ],
      "comments": []
    }
  ],
  "analysis": {
    "high_level_summary": "",
    "entry_points": [],
    "execution_paths": [],
    "data_flows": [],
    "external_calls": [],
    "risky_or_complex_nodes": [],
    "manual_check_required": []
  }
}
```

------

## 8. 人类可读摘要要求

必须输出 `summary.md`，面向人类和其他 Agent 阅读。

内容至少包括：

```markdown
# Blueprint Analysis Summary

## 1. Asset Overview
说明蓝图名称、类型、父类、接口、组件、依赖。

## 2. Graph Overview
列出 EventGraph、Function Graph、Macro Graph、Delegate Signature Graph 等。

## 3. Entry Points
列出 BeginPlay、Tick、Custom Event、Input Event、Dispatcher Event 等入口。

## 4. Main Execution Flow
用自然语言解释主要执行流。

## 5. Data Flow
说明关键变量、Struct、Array/Set/Map、Object Reference 的数据流。

## 6. Important Nodes
列出 Cast、Interface Call、Dispatcher、Latent、Timeline、Macro、Loop、Branch 等关键节点。

## 7. Risky / Complex Areas
标注解析器容易出错的位置。

## 8. Machine-Readable Outputs
列出 JSON、DOT、Mermaid、PNG/SVG 路径。

## 9. Confidence and Limitations
说明哪些部分确定，哪些需要人工确认。
```

------

## 9. 可视化要求

必须输出 DOT 和 Mermaid。

可视化中应区分：

- Exec 边
- Data 边
- Delegate/Event 边
- Object Reference 边
- Interface Message 边
- Cast 输入边
- Latent continuation 边
- Reroute/Knot 边
- Comment 区域
- 不同 Graph

建议视觉规则：

```text
Exec: solid
Data: dashed
Delegate/Event: bold or dotted
Latent: dotted with label latent
Object Reference: dashed with label object ref
Interface Message: label interface target
Cast: label cast object input
Reroute: small connector node
```

如果无法渲染 PNG/SVG，不要标记失败主流程，只要 DOT/Mermaid 已生成，并在 manifest 中标记：

```json
"png": "",
"svg": "",
"manual_check_required": ["PNG/SVG rendering requires Graphviz or Mermaid CLI"]
```

------

## 10. 其他 Agent 调用能力验证

本阶段必须确认该 Agent 是否能被其他 Agent 正确调用。

请新增或验证一个调用协议文档：

```text
docs/agent_call_contract.md
```

内容必须说明：

1. 如何调用该 Agent 分析一个 Blueprint。
2. 输入参数有哪些。
3. 输出文件有哪些。
4. 状态码如何解释。
5. JSON schema 如何读取。
6. 失败时如何判断原因。
7. 其他 Agent 应该读取哪个文件作为主入口。

建议主入口文件为：

```text
manifest.json
```

其他 Agent 的推荐流程：

```text
1. 调用 analyze_blueprint.ps1 或 Commandlet。
2. 读取 manifest.json。
3. 如果 status=success，读取 blueprint_ir.json 和 summary.md。
4. 如果 status=partial，读取 warnings.json 和 manual_check_required。
5. 如果 status=failed，读取 errors.json 和 analyze_log.txt。
6. 如需结构图，读取 viz/blueprint.mmd 或 viz/blueprint.dot。
```

------

## 11. 自测任务

请使用至少 3 类蓝图进行自测：

1. 一个简单测试蓝图，例如：
   `/Game/BPParserTest/BP_01_PrimitivePins_Basic`
2. 一个复杂测试蓝图，例如：
   `/Game/BPParserTest/BP_08_ComplexGameplayLikeGraph`
3. 一个用户随机提供或项目中已有的真实蓝图，例如：
   `【用户提供 AssetPath】`

如果用户还没有提供真实蓝图路径，请先用 `/Game/BPParserTest/BP_04_ExecFlow_Control` 作为占位自测，并在报告中说明仍需用户提供真实业务蓝图进行第二轮验证。

每次自测都要输出：

```text
asset_path
status
graph_count
node_count
pin_count
edge_count
warnings
errors
manual_check_required
output_dir
```

------

## 12. 结构理解能力评估

解析完成后，不仅要输出结构，还要评估自己对该蓝图的理解质量。

请输出：

```json
{
  "understanding_score": {
    "graph_discovery": "complete|partial|failed",
    "node_discovery": "complete|partial|failed",
    "pin_discovery": "complete|partial|failed",
    "edge_discovery": "complete|partial|failed",
    "variable_discovery": "complete|partial|failed",
    "function_discovery": "complete|partial|failed",
    "visualization": "complete|partial|failed",
    "agent_callable": "complete|partial|failed"
  },
  "confidence": 0.0,
  "limitations": [],
  "next_actions": []
}
```

该评分必须基于实际输出，不要主观虚高。

------

## 13. 问题分类与修复

如果解析某个已有蓝图时出问题，请不要只针对当前蓝图修补。

必须分析：

1. 是资产加载问题？
2. 是 Blueprint 类型不支持？
3. 是 Graph 类型没枚举全？
4. 是节点类型没识别？
5. 是 Pin 类型序列化不完整？
6. 是边分类错误？
7. 是 Reroute 处理不稳定？
8. 是 Macro / Delegate / Interface / Timeline / Widget / Anim Graph 特殊结构没有处理？
9. 是输出 JSON schema 不足？
10. 是其他 Agent 调用协议不清晰？

然后按通用方式修复：

- 更新解析器；
- 更新 schema；
- 更新可视化生成器；
- 更新调用脚本；
- 更新 `docs/agent_call_contract.md`；
- 更新 `docs/issue_patterns.md`；
- 增加回归用例。

------

## 14. 成功标准

只有满足以下条件，才能说本阶段通过：

1. 能通过 AssetPath 定位 Blueprint 资产。
2. 能读取真实蓝图，而不是读取 expected_ir。
3. 能输出合法 `manifest.json`。
4. 能输出合法 `blueprint_ir.json`。
5. 能输出人类可读 `summary.md`。
6. 能输出 DOT 和 Mermaid。
7. 能明确标记无法解析或需人工确认的部分。
8. 能被其他 Agent 通过 CLI 调用。
9. 调用方可以只读 `manifest.json` 判断任务是否成功。
10. 对简单蓝图和复杂蓝图都能工作。
11. 对用户随机提供的已有蓝图能给出稳定结构，哪怕部分内容需要人工确认。
12. 发现问题时能沉淀为通用解析能力改进，而不是当前资产特例修复。

------

## 15. 最终输出格式

完成后，请输出：

```text
# Blueprint Understanding Validation Report

## 1. 本阶段目标
说明本阶段验证的能力。

## 2. 已实现或确认的调用入口
列出 analyze_blueprint.ps1、Commandlet、参数、退出码。

## 3. 自测资产
列出测试过的 AssetPath。

## 4. 输出产物
列出 manifest、blueprint_ir、summary、DOT、Mermaid、PNG/SVG。

## 5. 结构理解结果
说明 Graph / Node / Pin / Edge / Variable / Function / Dispatcher 等解析情况。

## 6. 可视化结果
说明可视化是否生成，是否需要本地渲染依赖。

## 7. Agent 调用协议
说明其他 Agent 如何调用、读取什么文件、如何判断失败。

## 8. 发现的问题与通用修复
列出问题类型、根因和通用修复。

## 9. 仍需人工确认
列出需要我打开 UE 查看或提供截图的地方。

## 10. 下一步建议
说明是否可以进入真实项目蓝图批量解析阶段。
```

------

## 16. 禁止事项

1. 不要把 expected_ir 当作真实解析结果。
2. 不要伪造对已有蓝图的理解。
3. 不要只输出自然语言，不输出 JSON。
4. 不要只输出 JSON，不解释蓝图作用。
5. 不要忽略 Graph 类型。
6. 不要忽略 Pin 默认值和未连接 Pin。
7. 不要忽略 Reroute、Delegate、Macro、Interface、Latent 等高风险结构。
8. 不要硬编码 `/Game/BPParserTest/`。
9. 不要只支持当前测试蓝图，不支持用户随机蓝图。
10. 不要让其他 Agent 无法判断调用是否成功。