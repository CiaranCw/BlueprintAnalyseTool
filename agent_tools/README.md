# `blueprint_agent_tools`

Agent 侧 Python 工具包。提供给后续 Agent 直接调用的 12 个查询 / 解析工具。

## 安装

```powershell
cd agent_tools
pip install -e .[dev]
```

## 入口

```python
from blueprint_agent_tools.query_api import QueryAPI

api = QueryAPI(output_dir=r"D:\bp_out\MyGame")
api.list_blueprint_assets()
```

## 模块速览

| 模块                | 职责                                     |
| ------------------- | ---------------------------------------- |
| `ue_runner`         | 调起 `UnrealEditor-Cmd -run=BPATDump`    |
| `ir_client`         | 读取已 dump 的 IR 文件                   |
| `indexer`           | 基于 IR 构建索引（SQLite + JSON）        |
| `slicer`            | 子图切片                                 |
| `query_api`         | Agent 顶层查询入口（统一响应外壳）        |
| `schema_validator`  | JSON Schema 校验                         |
| `errors`            | 错误码定义                               |
| `output_layout`     | 输出目录布局解析                         |

## 工具清单

参见 [`blueprint_tool_manifest.json`](blueprint_tool_manifest.json) 与上层
[`docs/agent_tools.md`](../docs/agent_tools.md)。

## 安全约束

- 默认全部工具 `side_effect_level=read_only`；`dump_*` 例外为 `writes_output_only`。
- 当前阶段不允许出现 `modifies_project_files` / `modifies_assets` 工具。
