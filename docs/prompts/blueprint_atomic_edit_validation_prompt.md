# Blueprint Atomic Edit Capability Validation Prompt

你现在进入“蓝图原子编辑能力验证模式”。

这个阶段的目标是验证 Blueprint Agent 是否能够把对已有蓝图的高层修改需求，拆解成安全、可验证、可回滚、可复用的原子操作，并在 Unreal Engine 中正确落地。

这不是简单地“删几个节点、加几个节点、改几条线”。你需要确保每一个修改都能被抽象成通用能力，并考虑 UE Blueprint 中节点、Pin、连线、类型推断、Graph 重建、编译、保存、回滚和其他 Agent 调用的完整流程。

------

## 1. 任务背景

其他 Agent 或用户可能会提出类似需求：

```text
1. 删除这个蓝图中的某几个 PrintString 节点。
2. 在 BeginPlay 后面添加一个 Branch。
3. 将 Cast 成功分支后接到新的函数调用。
4. 把某条 Data 连线从 A 节点改接到 B 节点。
5. 在某个 ForEachLoop 的 LoopBody 中插入一个新节点。
6. 删除某个节点，同时保持前后 Exec 流连通。
7. 替换某个函数调用节点为另一个函数调用节点。
8. 将某个变量默认值改掉。
9. 添加一个新的变量并接入已有节点。
10. 修改一个 Dispatcher 绑定逻辑。
```

你的目标是确认：当前 Agent 是否能将这些高层修改要求拆成原子操作，生成编辑计划，执行修改，验证结果，并输出可供其他 Agent 使用的机器可读报告。

------

## 2. 总目标

给定一个 Blueprint AssetPath 和一组修改要求后，你需要完成：

1. 读取当前蓝图真实结构。
2. 建立编辑前基线 IR。
3. 将高层修改需求拆分成原子操作。
4. 对每个原子操作进行前置条件检查。
5. 按正确顺序执行操作。
6. 处理 UE Blueprint 连线替换、断链、类型推断、节点重建等问题。
7. 修改后重新解析蓝图。
8. 对比修改前后 IR。
9. 编译并保存蓝图。
10. 输出人类可读报告。
11. 输出机器可读 edit plan / edit result / diff report。
12. 支持被 Claude Code CLI、Cursor Agent 或其他 Agent 调用。

------

## 3. 关键原则

请严格遵守：

1. 不要直接按自然语言改蓝图，必须先生成编辑计划。
2. 不要在没有读取当前真实蓝图结构的情况下修改。
3. 不要凭 expected_ir 修改真实蓝图。
4. 不要硬编码当前蓝图名、节点名或本地路径。
5. 不要只修当前需求，要抽象出可复用的原子编辑能力。
6. 不要在不验证 Pin 类型和连接合法性的情况下连接节点。
7. 不要忽略 UE 中同一个输入 Pin 通常只能接一条线的替换行为。
8. 不要忽略 Exec 链断裂、Data 链替换、Delegate Pin 断连、Wildcard 类型推断失败等副作用。
9. 不要把修改成功伪造成编译成功。
10. 每次修改前都必须有备份或回滚策略。
11. 每次修改后都必须重新解析、对比、编译、保存。
12. 所有关键结果必须机器可读，便于其他 Agent 调用。

------

## 4. 输入参数

应支持以下输入：

```text
AssetPath: /Game/...
EditRequest: <自然语言修改需求或结构化修改需求>
ProjectUProject: <optional>
UERoot: <optional>
OutputDir: <optional>
Mode: plan-only | apply | apply-and-verify | dry-run
Strict: true | false
CreateBackup: true | false
AllowDestructiveEdit: true | false
```

如果没有明确允许破坏性修改，不得直接删除节点或断开连线，只能输出编辑计划或进行 dry-run。

------

## 5. 推荐 CLI 调用形式

必须提供或维护一个可被其他 Agent 调用的脚本入口，例如：

```powershell
.\scripts\edit_blueprint.ps1 `
  -UERoot "<UE_ROOT>" `
  -ProjectUProject "<PROJECT_UPROJECT>" `
  -AssetPath "/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -EditRequestJson ".\edit_requests\request_001.json" `
  -OutputDir "<PROJECT>\Saved\BPParserAgentReports" `
  -Mode "apply-and-verify" `
  -CreateBackup
```

也可支持 Commandlet：

```powershell
& "<UE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "<PROJECT_UPROJECT>" `
  -run=BPATEdit `
  -AssetPath="/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -EditRequestJson="...\request_001.json" `
  -OutputDir="...\Saved\BPParserAgentReports" `
  -Mode="apply-and-verify" `
  -CreateBackup=1 `
  -unattended `
  -nop4
```

如果这些入口不存在，请补齐；如果已有但不完整，请修复到可被其他 Agent 稳定调用的程度。

------

## 6. 编辑请求 JSON 格式

自然语言请求可以作为输入，但最终必须转成结构化编辑请求。

建议格式：

```json
{
  "schema_version": "1.0",
  "asset_path": "/Game/BPParserTest/BP_04_ExecFlow_Control",
  "intent": "Insert a Branch after BeginPlay and route true to PrintString",
  "mode": "apply-and-verify",
  "allow_destructive_edit": false,
  "create_backup": true,
  "operations": [
    {
      "op_id": "op_001",
      "operation": "insert_node_between",
      "graph": "EventGraph",
      "match": {
        "from_node": {
          "node_class": "K2Node_Event",
          "node_title_contains": "BeginPlay"
        },
        "to_node": {
          "node_class": "K2Node_CallFunction",
          "function_name": "PrintString"
        },
        "edge_type": "exec"
      },
      "new_node": {
        "node_class": "K2Node_IfThenElse",
        "node_title": "Branch",
        "position_strategy": "between"
      },
      "connect": [
        {
          "from": "BeginPlay.Then",
          "to": "Branch.Execute",
          "edge_type": "exec"
        },
        {
          "from": "Branch.Then",
          "to": "PrintString.Execute",
          "edge_type": "exec"
        }
      ],
      "defaults": {
        "Branch.Condition": true
      }
    }
  ]
}
```

如果用户只给自然语言，你要先生成这个 JSON，并在 `plan-only` 或确认步骤中展示。

------

## 7. 原子操作类型

你需要支持并验证以下原子操作能力。

## 7.1 读取类操作

- `load_blueprint`
- `dump_ir`
- `find_graph`
- `find_node`
- `find_pin`
- `find_edge`
- `find_variable`
- `find_function`
- `find_dispatcher`
- `find_component`

## 7.2 节点类操作

- `add_node`
- `remove_node`
- `replace_node`
- `move_node`
- `rename_node_comment`
- `set_node_comment`
- `set_node_enabled_state`
- `reconstruct_node`
- `refresh_node_signature`

## 7.3 Pin 类操作

- `set_pin_default_value`
- `clear_pin_default_value`
- `promote_pin_type`
- `validate_pin_type`
- `refresh_pin_type`
- `set_advanced_pin_visibility`

## 7.4 连线类操作

- `connect_pins`
- `disconnect_pins`
- `replace_edge`
- `insert_node_between`
- `splice_exec_chain`
- `preserve_exec_chain_on_delete`
- `rewire_data_dependency`
- `connect_through_reroute`
- `disconnect_all_input_links`
- `disconnect_all_output_links`

## 7.5 变量 / 函数 / 宏 / Dispatcher 操作

- `add_variable`
- `remove_variable`
- `set_variable_default`
- `change_variable_type`
- `add_function`
- `remove_function`
- `add_macro`
- `remove_macro`
- `add_event_dispatcher`
- `bind_event_dispatcher`
- `unbind_event_dispatcher`

## 7.6 结构化操作

- `add_comment_box`
- `add_reroute_node`
- `add_custom_event`
- `add_interface_call`
- `add_cast`
- `add_delay_or_timer`
- `add_container_operation`
- `add_make_break_struct`

## 7.7 验证类操作

- `compile_blueprint`
- `save_blueprint`
- `reload_blueprint`
- `redump_ir`
- `compare_ir`
- `validate_edit_result`
- `rollback_on_failure`

------

## 8. 原子操作计划格式

每次修改前必须生成 `edit_plan.json`。

格式建议：

```json
{
  "schema_version": "1.0",
  "asset_path": "/Game/BPParserTest/BP_04_ExecFlow_Control",
  "mode": "apply-and-verify",
  "preconditions": [
    {
      "check": "asset_exists",
      "status": "pending"
    },
    {
      "check": "graph_exists",
      "graph": "EventGraph",
      "status": "pending"
    }
  ],
  "atomic_operations": [
    {
      "op_id": "op_001",
      "operation": "find_edge",
      "phase": "precheck",
      "graph": "EventGraph",
      "inputs": {},
      "expected": {},
      "on_failure": "abort"
    },
    {
      "op_id": "op_002",
      "operation": "disconnect_pins",
      "phase": "apply",
      "inputs": {},
      "expected": {},
      "on_failure": "rollback"
    },
    {
      "op_id": "op_003",
      "operation": "add_node",
      "phase": "apply",
      "inputs": {},
      "expected": {},
      "on_failure": "rollback"
    },
    {
      "op_id": "op_004",
      "operation": "connect_pins",
      "phase": "apply",
      "inputs": {},
      "expected": {},
      "on_failure": "rollback"
    },
    {
      "op_id": "op_005",
      "operation": "compile_blueprint",
      "phase": "verify",
      "on_failure": "rollback"
    }
  ],
  "rollback_plan": [],
  "expected_diff": {}
}
```

------

## 9. 编辑顺序规则

UE Blueprint 中连线具有顺序敏感性，尤其是输入 Pin 只能接一条线时，连接顺序错误可能导致已有连接被替换或断裂。

因此必须遵守：

## 9.1 修改 Exec 链

插入节点到 A → B 之间时，不要直接先连 A → New，再连 New → B 而不处理旧线。

推荐顺序：

1. 找到 A.Then → B.Execute 旧边。
2. 记录旧边。
3. 创建新节点并刷新 Pins。
4. 断开 A.Then → B.Execute。
5. 连接 A.Then → New.Execute。
6. 连接 New.Then → B.Execute。
7. 验证 A → New → B 连通。
8. 确认没有意外多余 Exec 边。
9. 编译。

删除中间节点时：

1. 记录 Predecessor Exec Pin。
2. 记录 Successor Exec Pin。
3. 如果允许保持链路，先确认 Predecessor 和 Successor 类型可连接。
4. 断开 Node 输入和输出。
5. 删除节点。
6. 连接 Predecessor → Successor。
7. 验证链路。
8. 编译。

## 9.2 修改 Data 线

Data 输入 Pin 通常只能接一条线。

替换 A.Value → B.Input 为 C.Value → B.Input 时：

1. 检查 B.Input 当前连接。
2. 记录旧连接。
3. 检查 C.Value 与 B.Input 类型兼容。
4. 如需替换，先断开旧连接。
5. 再连接新连接。
6. 验证 B.Input 只连接到 C.Value。
7. 如果类型不兼容，尝试插入转换节点；如果无法自动转换，停止并报告。

## 9.3 Wildcard Pin

对 Array / Set / Map / Select / MakeArray / MakeMap 等 wildcard 节点：

1. 先用强类型上下文约束 wildcard。
2. 再设置默认值或连接 item/key/value。
3. 连接后重建节点。
4. 验证 PinCategory / ContainerType。
5. 不要在 wildcard 未定型时盲连。

## 9.4 Delegate / Event Pin

对 Dispatcher 相关编辑：

1. 先确认 Dispatcher 签名。
2. 再确认 Custom Event 参数完全匹配。
3. 创建或刷新 CreateDelegate。
4. 连接 CreateDelegate.OutputDelegate → AddDelegate.Delegate。
5. 校验 CallDelegate 参数。
6. 不匹配时不要强连。

## 9.5 Reroute / Knot

编辑经过 Reroute 的线时，要区分：

- 物理边：A → Knot → B
- 逻辑边：A → B

修改时应能选择：

- 保留 Reroute；
- 删除 Reroute 并直连；
- 新增 Reroute 以保持布局。

必须在 edit_plan 中说明策略。

------

## 10. 前置条件检查

每个原子操作执行前必须检查：

1. 目标资产存在。
2. 目标 Graph 存在。
3. 目标节点唯一或可消歧。
4. 目标 Pin 存在。
5. Pin 方向正确。
6. Pin 类型兼容。
7. 输入 Pin 是否已有连接。
8. 操作是否破坏 Exec 链。
9. 操作是否破坏 Data 依赖。
10. 操作是否影响 Dispatcher / Interface / Macro / Latent 特殊语义。
11. 是否允许破坏性修改。
12. 是否已创建备份。
13. 是否有 rollback plan。

如果任何关键前置条件不满足，不得执行 apply，只能输出失败原因和建议。

------

## 11. 备份与回滚

默认必须创建备份或可回滚状态。

建议：

1. 修改前导出 baseline IR。
2. 复制资产到备份路径，例如：
   `/Game/BPParserBackups/<AssetName>_<timestamp>`
3. 或保存外部 JSON diff / transaction。
4. 每个 atomic operation 都有 rollback operation。
5. 编译失败或验证失败时自动 rollback，除非 Mode 是 dry-run。

输出中必须说明：

- 备份路径；
- 是否成功创建；
- 回滚是否可用；
- 是否执行过回滚。

------

## 12. 修改后验证

每次 apply 后必须执行：

1. Reconstruct affected nodes。
2. Compile Blueprint。
3. Save Blueprint。
4. Redump IR。
5. Compare baseline IR vs modified IR。
6. 检查实际 diff 是否等于 expected_diff。
7. 检查是否出现意外新增/删除节点。
8. 检查是否出现意外断线。
9. 检查是否出现 Pin 默认值丢失。
10. 检查 Blueprint compile warning/error。
11. 输出 edit_result.json。

------

## 13. 输出文件规范

所有输出放入：

```text
Saved/BPParserAgentReports/<SanitizedAssetPath>/edits/<timestamp>/
```

至少包括：

```text
baseline_ir.json
edit_request.json
edit_plan.json
edit_result.json
diff_report.json
rollback_plan.json
modified_ir.json
summary.md
logs/edit_log.txt
viz/before.dot
viz/after.dot
viz/diff.mmd
```

如果可渲染，也输出：

```text
viz/before.png
viz/after.png
viz/diff.png
```

------

## 14. edit_result.json 格式

必须输出机器可读结果：

```json
{
  "schema_version": "1.0",
  "status": "success|partial|failed|rolled_back",
  "asset_path": "/Game/...",
  "mode": "apply-and-verify",
  "backup": {
    "created": true,
    "path": "/Game/BPParserBackups/..."
  },
  "operations": [
    {
      "op_id": "op_001",
      "operation": "find_edge",
      "status": "success|failed|skipped|rolled_back",
      "inputs": {},
      "outputs": {},
      "warnings": [],
      "errors": []
    }
  ],
  "validation": {
    "compile_status": "success|warning|failed|skipped",
    "save_status": "success|failed|skipped",
    "ir_redump_status": "success|failed|skipped",
    "diff_matches_expected": true,
    "unexpected_changes": []
  },
  "artifacts": {
    "baseline_ir": "",
    "modified_ir": "",
    "diff_report": "",
    "summary": "",
    "before_viz": "",
    "after_viz": "",
    "diff_viz": ""
  },
  "manual_check_required": []
}
```

------

## 15. diff_report.json 格式

必须输出结构化 diff：

```json
{
  "schema_version": "1.0",
  "asset_path": "/Game/...",
  "added_nodes": [],
  "removed_nodes": [],
  "modified_nodes": [],
  "added_edges": [],
  "removed_edges": [],
  "modified_edges": [],
  "modified_pins": [],
  "modified_variables": [],
  "modified_graphs": [],
  "unexpected_changes": [],
  "risk_notes": []
}
```

------

## 16. 原子能力自测

必须为原子编辑能力准备自测用例。至少覆盖：

1. `insert_node_between`：在 BeginPlay 和 PrintString 之间插入 Branch。
2. `remove_node_preserve_exec`：删除一个中间 PrintString 并保持 Exec 链。
3. `replace_data_edge`：替换某个 String 输入来源。
4. `set_pin_default_value`：修改未连接 Pin 默认值。
5. `add_variable_and_get_set`：添加变量并创建 Get/Set 节点。
6. `add_reroute_to_edge`：给现有连线加 Reroute。
7. `remove_reroute_preserve_connection`：删除 Reroute 并保持逻辑连线。
8. `add_container_operation`：新增 Array Add / Map Find 等容器节点并稳定 wildcard 类型。
9. `bind_event_dispatcher`：新增 Dispatcher 绑定或修改绑定。
10. `rollback_on_compile_failure`：故意制造失败，确认回滚有效。

自测结果必须输出到：

```text
Saved/BPParserAgentReports/atomic_edit_selftest/
```

------

## 17. 其他 Agent 调用协议

必须新增或更新：

```text
docs/agent_edit_contract.md
```

内容说明：

1. 其他 Agent 如何描述修改需求。
2. 自然语言需求如何转成 edit_request.json。
3. 哪些操作是安全操作。
4. 哪些操作需要 `AllowDestructiveEdit=true`。
5. 如何调用 `edit_blueprint.ps1`。
6. 如何读取 `edit_result.json`。
7. 如何判断修改成功、部分成功、失败或回滚。
8. 如何获取 diff_report。
9. 如何让用户最后在 UE 中人工确认。
10. 遇到失败时应该传回哪些文件。

------

## 18. 成功标准

只有满足以下条件，才能说原子编辑能力通过验证：

1. 能读取修改前蓝图真实结构。
2. 能生成 edit_plan.json。
3. 能执行 plan-only / dry-run。
4. 能在 apply 前做前置条件检查。
5. 能创建备份或回滚计划。
6. 能按正确顺序修改 Exec / Data / Delegate / Reroute 连线。
7. 能避免因输入 Pin 单连接特性导致意外断线。
8. 能重新编译和保存。
9. 能重新 dump IR。
10. 能输出 diff_report。
11. 能发现 unexpected_changes。
12. 能在失败时回滚。
13. 能输出 Agent 可读 edit_result.json。
14. 能用至少 3 个自测操作证明能力可用。
15. 修复和能力设计不硬编码当前工程。

------

## 19. 最终输出格式

完成后输出：

```text
# Blueprint Atomic Edit Capability Validation Report

## 1. 本阶段目标
说明验证的原子编辑能力。

## 2. 已实现或确认的调用入口
列出 edit_blueprint.ps1 / Commandlet / 参数 / 退出码。

## 3. 支持的原子操作
按读取、节点、Pin、连线、变量、函数、Dispatcher、验证分类列出。

## 4. 编辑顺序规则
说明 Exec、Data、Wildcard、Delegate、Reroute 的安全修改顺序。

## 5. 自测用例结果
列出每个自测是否通过。

## 6. 输出产物
列出 baseline_ir、edit_plan、edit_result、diff_report、viz。

## 7. 失败与回滚能力
说明如何触发、如何判断、如何读取结果。

## 8. 其他 Agent 调用协议
说明 Claude Code CLI / Cursor Agent 如何调用和读取结果。

## 9. 发现的问题与通用修复
列出本阶段发现的问题类型和已做的通用修复。

## 10. 仍需人工 UE 确认的内容
列出需要我打开 UE 检查的蓝图、节点、连线或截图。
```

------

## 20. 禁止事项

1. 不要直接修改蓝图而不生成编辑计划。
2. 不要在没有 baseline IR 的情况下修改。
3. 不要不检查 Pin 类型就连接。
4. 不要忽略输入 Pin 单连接替换问题。
5. 不要删除节点后造成 Exec 链断裂而不报告。
6. 不要为了当前蓝图硬编码节点 GUID。
7. 不要把 dry-run 当作真实 apply。
8. 不要把编译失败当作成功。
9. 不要没有 rollback plan 就执行破坏性操作。
10. 不要让其他 Agent 只能看自然语言结果，必须输出机器可读 JSON。