# 正确性校验策略

本项目不依赖大模型判断 IR 是否正确。所有 IR 必须通过四类机器化校验。

---

## 1. 结构完整性校验（`BPATIRValidator`）

在 `BPATIRSerializer` 落盘前调用。失败则把 `parse_status` 降级为 `partial`，并把违规写入 `errors[]`。

| 校验项                                    | 期望                                                      | 失败错误码                  |
| ----------------------------------------- | --------------------------------------------------------- | --------------------------- |
| `node_id` 唯一                            | 同一蓝图内不重复                                           | `BP_E_DUP_NODE_ID`          |
| `pin_id` 唯一                             | 同一节点内不重复                                           | `BP_E_DUP_PIN_ID`           |
| edge.from_node_id / to_node_id 都存在     | 在 `nodes[]` 中                                           | `BP_E_DANGLING_EDGE`        |
| edge.from_pin_id / to_pin_id 都存在       | 在对应节点 `pins[]` 中                                     | `BP_E_DANGLING_PIN_REF`     |
| Pin 方向匹配                              | output → input                                           | `BP_E_PIN_DIRECTION_MISMATCH` |
| Exec ↔ Data 不混连                        | exec 只连 exec，data 只连 data                             | `BP_E_EXEC_DATA_CROSS`      |
| 节点数量一致                              | IR 中节点数 == `UEdGraph::Nodes.Num()`                     | `BP_E_NODE_COUNT_MISMATCH`  |
| 变量 / 函数 / 宏可回溯                    | manifest 中每项能在 UBlueprint 内部找到对应                | `BP_E_MEMBER_NOT_TRACEABLE` |
| 实现接口可解析                            | `implemented_interfaces[]` 中 path 可解析为 UClass        | `BP_W_INTERFACE_UNRESOLVED` |

---

## 2. 只读安全校验（`BPATReadOnlyGuard` + `verify_readonly.py`）

### 2.1 进程内监控（UE 侧）

| 监控项                                 | 实现                                                                      |
| -------------------------------------- | ------------------------------------------------------------------------- |
| 写入原始 Content 目录                  | `BPATPathPolicy::AssertWritable` 拒绝 OutputDir 之外路径                  |
| 调用 SavePackage                       | 订阅 `UPackage::PackageSavedWithContextEvent`【待验证 V1】 + 路径白名单    |
| Modify Blueprint 状态                  | 不调用任何 `Modify()`、`MarkBlueprintAsModified` 等 API；编译期静态扫描   |
| 标记 Dirty                             | 订阅 `UPackage::PackageMarkedDirtyEvent` + 路径黑名单                      |
| 覆盖已有输出                           | `BPATOutputDirManager` 按 OverwritePolicy 校验                            |
| 未指定 overwrite 时写旧文件             | 默认 `skip` 时直接跳过，`fail` 时退出 30                                  |

监控失败一律：写 `level=fatal` 日志 → 退出码 40。

### 2.2 进程外校验（CI / 可选）

`scripts/verify_readonly.py`：

```text
1. 在 dump 前 snapshot Project 文件树（路径 + size + mtime + 可选 sha256）
2. 启动 dump
3. dump 后再 snapshot
4. diff 应为空集；非空则 CI 失败
```

---

## 3. 解析覆盖率校验（统计层）

`_summary.json` 中包含：

```json
{
  "total": 327,
  "success": 312,
  "partial": 14,
  "fatal": 1,
  "by_type": {
    "Blueprint": { "success": 280, "partial": 8 },
    "WidgetBlueprint": { "success": 22, "partial": 5 },
    "AnimBlueprint": { "success": 10, "partial": 1 },
    "Interface": { "success": 0 }
  },
  "common_failures": [
    { "code": "BP_E_NODE_COUNT_MISMATCH", "count": 3 }
  ],
  "unsupported_node_classes": [
    { "node_class": "K2Node_CustomFoo", "count": 17 }
  ],
  "unknown_pin_types": [
    { "category": "wildcard", "sub_category": "MyType", "count": 4 }
  ]
}
```

CI 阈值（默认）：

- `fatal` > 0 → 失败
- `partial / total > 5%` → warning
- 任意 `unsupported_node_classes` 累计计数 > 100 → warning（提示扩展节点支持）

---

## 4. IR Schema 校验（`bpat.schema_validator`）

所有产出 JSON（manifest / graph_summary / node_detail / slice / tool_response）落盘后必须通过对应 schema：

```python
from blueprint_agent_tools.schema_validator import SchemaValidator

v = SchemaValidator()
v.validate("manifest", "<OutputDir>/.../manifest.json")
v.validate("graph_summary", "<OutputDir>/.../graphs/graph_0001.summary.json")
```

CI 应：
- 对 `examples/sample_outputs/` 的 golden 全量校验
- 对 `tests/ir_validation/fixtures/` 的合法 + 故意非法分别校验通过 / 拒绝

---

## 5. 校验组合策略

```text
[单蓝图 dump]
    BPATIRValidator (运行时)
        → 失败 → status=partial, 仍写出 IR
[落盘后]
    bpat.schema_validator (Python)
        → 失败 → status 升级为 fatal
[Agent 调用 validate_blueprint_ir]
    同时跑结构 + Schema
[CI 回归]
    + readonly snapshot diff
    + golden_ir_diff（IR 与 git 中标杆对比）
    + tool_response 合约测试
```

---

## 6. golden IR 测试

- `tests/ir_validation/fixtures/<asset>/expected.manifest.json`
- 比较时忽略 `parse_time`、`source_fingerprint.uasset_mtime`、`elapsed_ms`
- 其它字段必须完全相等
