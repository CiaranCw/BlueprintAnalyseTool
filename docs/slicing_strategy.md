# 大蓝图分层与切片策略

某些蓝图的节点数 / 图数量超出 Agent 单次上下文。本文规定如何分层输出 IR 与按需切片。

---

## 1. 四层输出

| 层           | 文件                                           | 用途                                       |
| ------------ | ---------------------------------------------- | ------------------------------------------ |
| Manifest     | `manifest.json`                                | 蓝图总体结构（必有）                       |
| Graph Summary| `graphs/<id>.summary.json`                     | 单图概要（必有）                           |
| Graph Full   | `graphs/<id>.full.json`                        | 单图完整（小图）                           |
| Node Detail  | `nodes/<id>.json`                              | 单节点详情（大图按需）                     |
| Slice        | `slices/<hash>.json`                           | 切片缓存                                   |

### 触发阈值（默认，可配置）

```text
node_count <= 200  → graphs/<id>.full.json 直接 inline 节点详情
node_count >  200  → 不 inline；nodes/<id>.json 单文件
total_nodes > 1000 → 全分层模式（manifest + summary + node detail，不写 full）
```

---

## 2. Manifest 层内容

让 Agent 一次拿到全局结构（详见 `blueprint_ir_schema.md` 第 1–2 节）：

- 资产路径 / 父类 / 蓝图类型 / 引擎版本
- 图列表（id, name, type, node_count, edge_count, summary_path）
- 变量 / 函数 / 宏 / 事件分发器 / 接口 / Timeline 列表
- 组件树（仅 Actor 类）
- 复杂度统计：`graph_count`, `total_nodes`, `total_edges`, `max_graph_depth`

---

## 3. Graph Summary 层内容

- `entry_nodes`：事件 / 函数入口节点 id
- `node_type_distribution`：每类节点的数量直方图
- `called_functions`：被调函数 + 调用次数
- `read_variables` / `written_variables`
- `external_object_refs`：跨蓝图 / 跨资产引用
- `exec_chain_summaries`：从每个入口出发，深度受限的执行链摘要
  ```json
  {
    "from_event": "node_0001",
    "depth": 7,
    "branch_count": 2,
    "ends_at": ["node_0023"]
  }
  ```
- `complex_regions`：邻居数 ≥ 阈值的"枢纽节点"
- `graph_warnings`

---

## 4. Node Detail 层

详见 `blueprint_ir_schema.md` 第 4 节。除完整节点 / Pin 信息外还包含：

- `upstream_nodes`：直接通过 exec / data 边连入的节点 id 集合
- `downstream_nodes`：直接连出的节点 id 集合
- `containing_subgraph_id`：所在局部子图（若有）

---

## 5. 切片机制（`bpat.slicer`）

切片输出**必须是合法子图**：节点 + 边 + 边界注解，所有 id 都能回溯到原始 IR。

### 5.1 切片维度

| 切片名               | 输入                                                | 输出                               |
| -------------------- | --------------------------------------------------- | ---------------------------------- |
| `trace_exec_flow`    | `start_node_id, max_depth`                          | 沿 exec 边正向追踪                 |
| `trace_data_deps`    | `target_node_id, max_depth`                         | 沿 data 边反向追踪（依赖来源）     |
| `trace_variable_usage` | `variable_name`                                  | 全图所有读 / 写该变量的节点         |
| `trace_function_callers` | `function_path`                                | 调用该函数的节点                    |
| `trace_widget_binding` | `widget_name`                                  | partial：UI 控件 → 蓝图事件        |
| `trace_input_handler` | `input_action_name`                              | partial：输入事件 → 后续行为        |
| `slice_subgraph`     | `start_node_id, direction, depth, edge_filter`      | 通用 BFS / DFS                     |

### 5.2 切片输出 schema

```json
{
  "schema_version": "0.1.0",
  "slice_kind": "trace_exec_flow",
  "origin": { "asset_path": "/Game/...", "graph_id": "graph_0001", "start_node_id": "node_0001" },
  "params": { "direction": "forward", "depth": 8, "edge_filter": "exec" },
  "nodes": [ { "node_id": "...", "node_class": "...", "title": "...", "ref_to_node_detail": "nodes/node_0001.json" } ],
  "edges": [ { "edge_id": "...", "from_node_id": "...", "to_node_id": "...", "edge_kind": "exec" } ],
  "boundary": {
    "truncated_at_depth": [ { "node_id": "node_0042", "reason": "depth_limit" } ],
    "external_references": [ { "node_id": "node_0050", "reason": "cross_graph" } ]
  },
  "stats": { "node_count": 28, "edge_count": 35, "elapsed_ms": 4 }
}
```

切片必须满足**回溯性**：每个节点都附 `ref_to_node_detail` 指向原始 IR 文件相对路径，Agent 可据此回查全字段。

### 5.3 切片缓存

- 切片 hash = `sha256(slice_kind + params_canonical_json + ir_fingerprint)`
- 命中缓存直接返回 `slices/<hash>.json`
- 缓存默认 ON；显式 `cache=false` 跳过

### 5.4 切片合法性

切片必须保证：

1. 所有边的 `from_node_id` / `to_node_id` 都在 `nodes` 列表中（除非显式标在 `boundary.external_references`）。
2. 不会随机抽取节点构成"非连通碎片"——必须是从 origin 起按方向 BFS / DFS 的连通子图。
3. 边界节点必须显式列在 `boundary.truncated_at_depth` 或 `boundary.external_references` 中，便于 Agent 知道"还可以继续展开"。

---

## 6. 大蓝图调用建议（给 Agent）

```text
1. 永远先 get_blueprint_manifest 拿全局
2. 对感兴趣的图调 get_graph_summary 而不是 graphs/<id>.full.json
3. 进入具体节点时调 get_node_detail，单次只取 1 个
4. 涉及多节点关系时调 trace_exec_flow / trace_variable_usage 而不是手动拼接
5. 控制 depth ≤ 10；优先窄 depth + 多次切
```
