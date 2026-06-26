# 反向构建路线图（下一阶段预留）

> **当前阶段不实现反向构建。** 本文只规定接口与字段层面的预留与硬约束，避免现在做无用功，又能让下一阶段顺利接入。

---

## 1. 反向流程总图（仅设计意图）

```text
BlueprintSpec (自然语言或结构化输入)
    ↓
SpecParser
    ↓
BlueprintIR (与正向阶段同 schema)
    ↓
ReverseValidator (字段必填、节点工厂可达)
    ↓
SandboxProjectMaker (复制原工程到沙箱)
    ↓
NodeFactory.AllocateDefaultPins
    ↓
EdgeBuilder.TryCreateConnection
    ↓
DefaultValueApplier
    ↓
SandboxedKismetCompiler
    ↓
SavePackage (仅写入沙箱 Project)
    ↓
DiffReporter (沙箱 vs 原工程)
```

---

## 2. IR 字段层预留

manifest 顶层固定：

```json
"reverse_generation_reservation": {
  "construction_recipe": null,
  "compile_options": null,
  "node_creation_hints": null,
  "pin_default_overrides": null,
  "current_phase_writes_them": false
}
```

每个 node：

```json
"reverse_hints": null
```

每条 edge：

```json
"creation_order_hint": null
```

> 当前阶段全部为 `null`。下一阶段不破坏 `schema_version` 主版本即可填入。

---

## 3. 目录层预留

```text
ue_plugin/BlueprintAgentTools/Source/
├─ BlueprintAgentToolsEditor/                       当前阶段
└─ BlueprintAgentToolsAuthoring/  ← 反向阶段新增（当前不创建）
   ├─ Public/
   │  ├─ Recipes/        # BlueprintSpec → 节点配方
   │  ├─ Builders/       # 反向构建器
   │  ├─ Compilers/      # 编译封装（SavePackage 隔离）
   │  └─ Sandbox/        # 沙箱机制
   └─ Private/
```

```text
agent_tools/src/blueprint_agent_tools/
├─ authoring/            ← 反向阶段新增（当前不创建）
│  ├─ spec_parser.py
│  ├─ build_orchestrator.py
│  └─ sandbox_runner.py
```

---

## 4. 接口层预留

`agent_tools/blueprint_tool_manifest.json` 中：

```json
{
  "namespaces": {
    "blueprint": { "tools": [ ... 当前 12 个 ... ] },
    "authoring": { "reserved": true, "tools": [] }
  }
}
```

下一阶段拟注册：

| 工具                              | 用途                                | side_effect_level                      |
| --------------------------------- | ----------------------------------- | -------------------------------------- |
| `compile_blueprint_in_sandbox`    | 沙箱内编译 IR → 蓝图                 | `modifies_assets`（仅沙箱）            |
| `materialize_blueprint_from_ir`   | 沙箱内实例化 IR 为 .uasset           | `modifies_assets`（仅沙箱）            |
| `diff_blueprints_by_ir`           | 同 IR 比对                          | `read_only`                            |
| `apply_blueprint_patch_to_sandbox`| 在已有沙箱蓝图上打补丁              | `modifies_assets`（仅沙箱）            |

---

## 5. 沙箱硬约束

下一阶段反向工具**永远不允许**作用于原始工程。强制规则：

1. **必须显式提供 `--sandbox-project`**，且该路径不得等于 `--project`。
2. `--sandbox-project` 必须由本工具脚本（`scripts/make_sandbox_project.py`）创建：
   - 复制 `<project>` 全部内容到目标位置
   - 写一份 `<sandbox>/.bpat_sandbox.json` 标记，包含原工程哈希、创建时间
3. 反向工具启动时校验 `<sandbox>/.bpat_sandbox.json` 存在；缺失则拒绝运行。
4. `BPATPathPolicy` 增加"沙箱写白名单"模式，仅对 `<sandbox-project>` 内路径解禁；原 `<project>` 仍然只读。
5. 反向工具的 `side_effect_level=modifies_assets` 永远不可作用在 `<project>` 上。

---

## 6. 反向校验层预留

下一阶段引入 `BPATIRValidatorReverse`，校验：

- IR 是否能被反向构建器消费（必填字段齐全）
- `node_factory` 在当前 UE 版本可用
- `expected_pin_layout` 与节点反向构造后实际 Pin 一致
- `creation_order_hint` 形成的连接顺序在 `UEdGraphSchema_K2::TryCreateConnection` 下成功率 ≥ 阈值

当前阶段 `BPATIRValidator` 只对 `reverse_generation_reservation` 做"存在即合法"的弱校验。

---

## 7. 沙箱编译失败的回滚策略

- 沙箱在反向开始前由脚本生成；任何失败 → **删除整个沙箱目录**而不是回滚单文件
- 反向工具不允许执行 `git`、`p4`、`svn` 等 VCS 命令
- 反向工具不允许使用 `--sandbox-project=<project>`（即原地修改）

---

## 8. 当前阶段不做的事（明确清单）

- 不实现 `compile_blueprint_in_sandbox` / `materialize_blueprint_from_ir`
- 不实现 `BPATIRValidatorReverse`
- 不创建 `BlueprintAgentToolsAuthoring` 模块
- 不写 `agent_tools/.../authoring/` 包
- 不在任何工具里允许 `--sandbox-project` 参数
- `reverse_generation_reservation` 字段当前阶段恒为 `null`，但 schema 必须接受其存在（向后兼容）

---

## 9. 通往反向阶段的入口标准

下一阶段启动前必须满足：

1. 当前阶段所有 P0 测试 100% 通过
2. golden_ir_diff 在标杆工程上稳定
3. `_summary.json.partial / total` < 5%
4. `bpat.schema_validator` 对全 fixture pass
5. 文档中所有【待验证】项目已标注 `[已验证]`

否则不应启动反向阶段。
