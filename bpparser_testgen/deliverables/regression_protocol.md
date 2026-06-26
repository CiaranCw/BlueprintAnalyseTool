# 回归解析协议（你修改蓝图后如何交给我重新解析）

## 1. 流程
1. 在 UE 中打开 `/Game/BPParserTest/` 下的某个蓝图，做少量修改（加/删节点、改连线、改默认值、改变量类型、加注释等）。
2. **保存**资产。
3. 用 BPAT 解析器导出该蓝图的真实 IR（见下「导出命令」），或直接把修改说明 + 蓝图截图 + 导出的 IR JSON 发给我。
4. 我会：解析修改后蓝图 → 输出新 IR JSON → 与 `deliverables/expected_ir/<asset>.json` 基线对比 → 输出差异报告 → 重新生成可视化。

## 2. 用 BPAT 解析器导出真实 IR（推荐，作为权威输入）
BPAT 是只读解析器（本仓库 `ue_plugin/BlueprintAgentTools`）。导出单个蓝图：

```powershell
& "<UE_5.4>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "E:\BPTestProject\BPTest\BPTest.uproject" `
  -run=BPATDump `
  -AssetPath=/Game/BPParserTest/BP_01_PrimitivePins_Basic `
  -OutputDir=E:\bp_out\BPTest -StrictReadOnly=1
```
把 `E:\bp_out\BPTest\...\manifest.json` + `graphs/*` 发我即可。

> 若 BPAT 的 IRSerializer 尚未落地（当前为骨架），则改用「方式 B」：直接发我修改说明 + 蓝图截图/节点列表，我按 expected_ir 基线推断差异。

## 3. 我会输出的差异报告格式
```json
{
  "asset_name": "BP_01_PrimitivePins_Basic",
  "baseline": "deliverables/expected_ir/BP_01_PrimitivePins_Basic.json",
  "change_summary": {
    "added_nodes":     [ { "node_class": "", "node_name": "", "graph": "" } ],
    "removed_nodes":   [ { "node_name": "" } ],
    "modified_nodes":  [ { "node_name": "", "what": "comment/position/title changed" } ],
    "added_edges":     [ { "from": "", "to": "", "edge_type": "" } ],
    "removed_edges":   [ { "from": "", "to": "" } ],
    "modified_pins":   [ { "node": "", "pin": "", "from": "", "to": "", "kind": "default_value|type|container" } ],
    "modified_variables": [ { "name": "", "from_type": "", "to_type": "" } ],
    "graph_structure_changes": [ "added function graph X", "macro Y body wired" ]
  },
  "risk_notes": [
    "某 Pin 类型从 real/float 变为 real/double，可能影响解析器类型推断",
    "新增 wildcard 容器函数，注意容器类型解析",
    "委托 pin 连线变化，确认 delegate 边分类"
  ]
}
```

## 4. 对比规则（重要）
- **按结构匹配**，不按字面 ID：节点用 `(graph_name, node_class, node_name/title, 关键 pin 类型)` 匹配；边用 `(from_node 标识, from_pin_name, to_node 标识, to_pin_name)` 匹配。
- 引擎自动加的隐藏 pin / 高级 pin / 宏内部展开节点的差异，标为 `info` 而非 `change`。
- Pin 默认值变化、容器类型变化、变量类型变化 → 高优先 `risk_notes`。
- 若 graph 数量/类型变化（新增 Function/Macro/Delegate 图）→ 记入 `graph_structure_changes`。

## 5. 你发我时最好附带
- 改了哪个蓝图、改动意图（1-2 句）。
- 若有：BPAT 导出的 manifest/graphs JSON（最权威）。
- 若无导出：节点增删列表 + 截图即可。
