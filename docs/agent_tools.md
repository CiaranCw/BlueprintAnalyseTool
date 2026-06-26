# Agent 工具说明

本文档列出本项目对外暴露给 Agent 的全部工具。每个工具：
- 必须可在不依赖人工干预的前提下被 Agent 调用；
- 必须使用统一响应外壳；
- 当前阶段 `side_effect_level` 仅允许 `read_only` / `writes_output_only`。

---

## 1. 通用响应外壳

所有工具返回符合 [`tool_response.schema.json`](../agent_tools/schemas/tool_response.schema.json) 的结构：

```json
{
  "tool": "trace_exec_flow",
  "ok": true,
  "data": { ... },
  "errors": [],
  "warnings": [],
  "meta": {
    "from_cache": true,
    "schema_version": "0.1.0",
    "elapsed_ms": 12,
    "ue_invoked": false
  }
}
```

---

## 2. 工具清单

下表对应 `agent_tools/blueprint_tool_manifest.json` 的注册项。

### 2.1 写入类（`writes_output_only`）

#### `dump_blueprint`

- **用途**：解析单个蓝图，把 IR 写入 `OutputDir`。
- **输入**：
  ```json
  {
    "project": "D:\\MyGame\\MyGame.uproject",
    "asset_path": "/Game/Blueprints/BP_Hero",
    "output_dir": "D:\\bp_out\\MyGame",
    "overwrite": "skip",
    "layers": ["manifest", "graphs", "nodes"],
    "dry_run": false
  }
  ```
- **输出**：
  ```json
  {
    "status": "success",
    "asset_path": "/Game/Blueprints/BP_Hero",
    "output_paths": {
      "manifest": ".../manifest.json",
      "graphs_dir": ".../graphs/",
      "nodes_dir": ".../nodes/"
    },
    "warnings": [],
    "errors": []
  }
  ```
- **是否需要启动 UE**：是（必须）
- **是否使用缓存**：是（如果 `source_fingerprint` 命中且 `overwrite=skip`，跳过）
- **side_effect_level**：`writes_output_only`
- **典型场景**：Agent 第一次接触某蓝图，触发结构化解析。

#### `dump_project_blueprints`

- **用途**：批量解析全工程蓝图。
- **输入**：
  ```json
  {
    "project": "D:\\MyGame\\MyGame.uproject",
    "class_filter": ["Blueprint", "WidgetBlueprint", "AnimBlueprint"],
    "output_dir": "D:\\bp_out\\MyGame",
    "overwrite": "skip"
  }
  ```
- **输出**：`_summary.json` 内容 + 失败列表。
- **side_effect_level**：`writes_output_only`
- **典型场景**：项目接入时一次性建库。

---

### 2.2 只读类（`read_only`）

> 这类工具默认只读 `OutputDir/_index/` 与 `blueprints/`；不启动 UE。
> 若 IR 缺失会返回错误码 `E1002 IR_NOT_DUMPED`，Agent 决定是否调 `dump_blueprint` fallback。

#### `list_blueprint_assets`

- **输入**：`{ output_dir, filter? }`
- **输出**：`{ assets: [{asset_path, blueprint_class, parent_class, parse_status, complexity}] }`
- **典型场景**：Agent 需要决定从哪个蓝图开始。

#### `get_blueprint_manifest`

- **输入**：`{ output_dir, asset_path }`
- **输出**：`manifest.json` 全文。
- **典型场景**：概览蓝图结构。

#### `list_graphs`

- **输入**：`{ output_dir, asset_path }`
- **输出**：`{ graphs: [{graph_id, graph_name, graph_type, node_count, edge_count}] }`

#### `get_graph_summary`

- **输入**：`{ output_dir, asset_path, graph_name | graph_id }`
- **输出**：`graph_summary.json` 全文。
- **典型场景**：判断图复杂度、入口节点、关键调用。

#### `search_nodes`

- **输入**：
  ```json
  {
    "output_dir": "...",
    "asset_path": "/Game/...",
    "keyword": "ApplyDamage",
    "node_class": "K2Node_CallFunction",
    "limit": 50
  }
  ```
- **输出**：`{ matches: [{node_id, node_class, snippet, graph_id, score}] }`
- **典型场景**：定位"哪里调用了 X"。

#### `get_node_detail`

- **输入**：`{ output_dir, asset_path, node_id }`
- **输出**：node_detail JSON 全文。

#### `trace_exec_flow`

- **输入**：`{ output_dir, asset_path, start_node_id, max_depth }`
- **输出**：子图 JSON（`subgraph_slice.schema.json`）。
- **典型场景**：解释 BeginPlay 之后的执行流。

#### `trace_variable_usage`

- **输入**：`{ output_dir, asset_path, variable_name }`
- **输出**：`{ reads: [...], writes: [...] }`，每项含 `{node_id, graph_id, snippet}`。

#### `slice_subgraph`

- **输入**：`{ output_dir, asset_path, start_node_id, direction, depth, edge_filter? }`
  - `direction`：`forward` / `backward` / `both`
  - `edge_filter`：`exec` / `data` / `all`
- **输出**：合法子图 JSON。
- **典型场景**：受限上下文窗口下给 Agent 一份合法子图。

#### `validate_blueprint_ir`

- **输入**：`{ output_dir, asset_path }`
- **输出**：`{ ok, errors, warnings, schema_version }`
- **典型场景**：回归测试 / 调试。

---

## 3. side_effect_level 矩阵

| 工具                       | side_effect_level    | 当前阶段允许 |
| -------------------------- | -------------------- | ------------ |
| `dump_blueprint`           | `writes_output_only` | 是           |
| `dump_project_blueprints`  | `writes_output_only` | 是           |
| `list_blueprint_assets`    | `read_only`          | 是           |
| `get_blueprint_manifest`   | `read_only`          | 是           |
| `list_graphs`              | `read_only`          | 是           |
| `get_graph_summary`        | `read_only`          | 是           |
| `search_nodes`             | `read_only`          | 是           |
| `get_node_detail`          | `read_only`          | 是           |
| `trace_exec_flow`          | `read_only`          | 是           |
| `trace_variable_usage`     | `read_only`          | 是           |
| `slice_subgraph`           | `read_only`          | 是           |
| `validate_blueprint_ir`    | `read_only`          | 是           |
| `compile_blueprint_in_sandbox`   | `modifies_assets`（沙箱） | **否（反向阶段）** |
| `materialize_blueprint_from_ir`  | `modifies_assets`（沙箱） | **否（反向阶段）** |

---

## 4. 错误码

| 错误码 | 名称                       | 触发                                      | Agent 处理建议                              |
| ------ | -------------------------- | ----------------------------------------- | -------------------------------------------- |
| `E1001`| `ASSET_NOT_FOUND`          | asset_path 不存在                          | 先 `list_blueprint_assets`                  |
| `E1002`| `IR_NOT_DUMPED`            | 缓存里没有该蓝图 IR                       | fallback `dump_blueprint`                   |
| `E1003`| `SCHEMA_VERSION_MISMATCH`  | IR schema 版本与工具不匹配                | `dump_blueprint --overwrite=overwrite`      |
| `E1101`| `NODE_NOT_FOUND`           | node_id 不在该蓝图                        | 重 `search_nodes`                            |
| `E1102`| `VARIABLE_NOT_FOUND`       | 变量名不在 manifest                       | 列 `manifest.variables`                      |
| `E1201`| `SLICE_DEPTH_EXCEEDED`     | depth 超出上限                            | 减小 depth                                   |
| `E2001`| `READ_ONLY_VIOLATION`      | UE 解析期间检测到写入                     | 不重试；人工排查                             |
| `E2002`| `OUTPUT_DIR_INSIDE_PROJECT`| OutputDir 在 ProjectPath 内               | 改为项目外目录                               |
| `E2003`| `OVERWRITE_BLOCKED`        | 输出目录已有文件且策略=fail               | 选 skip 或 overwrite                         |
| `E5001`| `UE_LAUNCH_FAILED`         | UnrealEditor-Cmd 异常                     | 检查 .uproject                               |
| `E5002`| `UE_TIMEOUT`               | dump 超时                                  | 减小批量规模                                 |

---

## 5. 调用样例

### 5.1 Agent 完整一次"读懂某蓝图"

```python
from blueprint_agent_tools.query_api import QueryAPI

api = QueryAPI(output_dir=r"D:\bp_out\MyGame")

# 1) 列资产
assets = api.list_blueprint_assets(filter_class="Character")
# 2) 拿 manifest
manifest = api.get_blueprint_manifest(asset_path="/Game/Blueprints/BP_Hero")
# 3) 找入口
graphs = api.list_graphs(asset_path="/Game/Blueprints/BP_Hero")
# 4) 看图概要
summary = api.get_graph_summary(asset_path="/Game/Blueprints/BP_Hero", graph_name="EventGraph")
# 5) 沿入口追执行流
flow = api.trace_exec_flow(asset_path="/Game/Blueprints/BP_Hero",
                           start_node_id=summary["entry_nodes"][0]["node_id"],
                           max_depth=8)
```

### 5.2 IR 缺失时的 fallback

```python
res = api.get_blueprint_manifest(asset_path="/Game/Blueprints/BP_Boss")
if not res["ok"] and res["errors"][0]["code"] == "E1002":
    api.dump_blueprint(project=r"D:\MyGame\MyGame.uproject",
                       asset_path="/Game/Blueprints/BP_Boss",
                       output_dir=r"D:\bp_out\MyGame")
    res = api.get_blueprint_manifest(asset_path="/Game/Blueprints/BP_Boss")
```

---

## 6. 工具注册文件

唯一权威清单：[`agent_tools/blueprint_tool_manifest.json`](../agent_tools/blueprint_tool_manifest.json)。
