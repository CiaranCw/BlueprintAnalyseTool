你现在进入“本地验收问题诊断与通用修复模式”。

我正在 UE 编辑器中验收 `/Game/BPParserTest/` 下已经生成的蓝图测试资产。现在发现了警告、报错、缺少连线、节点失效、Pin 类型不对、宏节点异常、委托绑定异常、容器节点类型推断异常、蓝图无法编译或实际结构与 expected_ir 不一致等问题。

你的任务不是只针对当前这个现象做小修小补，而是要通过这个问题反推：为什么会出现这种类型的问题、它属于哪一类 UE Blueprint 节点 / Pin / 连线 / Graph 生成问题、当前生成器或解析器的哪一层设计有缺陷、这种问题在其他蓝图或其他 UE 工程中是否也可能出现，并基于通用原因修复自身功能。

# 一、针对我发现的问题

如果我只提供了截图、日志片段或简短描述，你需要尽可能从已有信息推断问题类型；如果信息不足，先基于最可能原因做静态分析和可修复项，不要直接停止。

# 二、首要原则

请严格遵守：

1. 不要只修当前蓝图的当前节点。
2. 不要为了让当前用例通过而硬编码当前蓝图名、节点名或本地路径。
3. 不要把问题归结为“UE 偶发”后跳过。
4. 不要伪造修复成功。没有重新验证的内容必须标记为“待本地验证”。
5. 如果是生成器问题，修生成器；如果是解析器问题，修解析器；如果是 expected_ir / 可视化 / 覆盖矩阵问题，同步修对应产物。
6. 修复必须面向通用 Agent 能力：迁移到其他同 UE 版本工程、甚至不同 UE 版本时仍尽量可用。
7. 每次修复都要形成一个可复用的“问题类型规则”，以后遇到同类节点或同类 Pin 时能提前规避。
8. 修复完成后必须新增或更新回归测试，防止同类问题再次出现。

# 三、诊断目标

针对本次问题，你必须回答并处理以下问题：

## 1. 这是什么类型的问题？

请将问题归类到以下一种或多种：

- C++ 插件编译问题
- UE 版本 API 差异问题
- 蓝图资产生成失败
- 节点创建失败
- 节点类型错误
- 节点签名未刷新
- Pin 找不到
- Pin 类型错误
- Pin 方向错误
- Pin 默认值错误
- Pin 需要类型推断但没有推断成功
- Wildcard Pin 没有被容器变量约束
- Exec 连线缺失
- Data 连线缺失
- Delegate/Event 连线缺失
- Object Target 连线缺失
- Cast Object 输入缺失
- Interface Message Target 缺失
- Latent 节点 Continuation 连线错误
- MacroInstance 解析失败
- StandardMacros 路径或宏名不兼容
- Event Dispatcher 创建或绑定失败
- Custom Event 参数不匹配
- Struct / Enum 创建失败
- Make Struct / Break Struct 字段 Pin 不一致
- Array / Set / Map 节点 wildcard 类型未稳定
- Function / Pure Function / Macro Graph 创建不完整
- Blueprint Compile 失败
- expected_ir 与实际蓝图结构不一致
- DOT / Mermaid 与 expected_ir 不一致
- 覆盖矩阵状态错误
- 文档或人工检查指南不准确
- 其他，请自行说明

## 2. 这个问题为什么会出现？

请不要只说“连线失败”或“节点异常”。你必须进一步分析：

- 是创建节点时没有调用正确的 UE API？
- 是节点创建后没有调用 `AllocateDefaultPins` / `ReconstructNode` / `PostPlacedNewNode`？
- 是 Pin 名称在 UE 版本间不同？
- 是节点的 Pin 需要通过类型上下文推断？
- 是 Wildcard Pin 没有先连接到强类型变量？
- 是连接顺序不对？
- 是 FunctionReference / MacroReference / DelegateReference 没有设置？
- 是自定义 Struct / Enum / Interface 尚未完成编译或刷新？
- 是 Blueprint 没有 MarkPackageDirty / Compile / Save？
- 是 Event Dispatcher 的签名图和绑定节点不一致？
- 是 StandardMacros 路径在当前 UE 版本中不一致？
- 是 expected_ir 中写了设计结构，但生成器实际没有生成？
- 是可视化图使用了理想结构，而不是实际生成结构？
- 是本地工程缺少插件或模块？
- 是本地 UE 版本 API 与生成器写法不兼容？

## 3. 这种问题可能出现在哪些节点或结构上？

请抽象出通用影响范围。

例如：

- 如果是 Wildcard Pin 类型推断问题，请检查所有 Array / Set / Map / MakeArray / MakeMap / Select / Equal / Add 等 wildcard 节点。
- 如果是 Delegate Pin 问题，请检查 CreateDelegate、AddDelegate、ClearDelegate、CallDelegate、CustomEvent 参数、Dispatcher 签名图。
- 如果是 MacroInstance 问题，请检查 DoOnce、DoN、FlipFlop、Gate、ForLoop、ForLoopWithBreak、ForEachLoop、ForEachLoopWithBreak、WhileLoop 等所有 StandardMacros。
- 如果是 Target Pin 问题，请检查 Interface Message、CallFunction on Object、Component Function、Cast 后调用、Self 调用。
- 如果是 Latent 节点问题，请检查 Delay、Timer、Timeline、Async Action。
- 如果是 Struct 字段问题，请检查 MakeStruct、BreakStruct、SetMembersInStruct、Struct 默认值、Struct 内容器字段。
- 如果是 Graph 类型问题，请检查 EventGraph、Function Graph、Macro Graph、Delegate Signature Graph、Interface Function Graph。

# 四、输入资料读取要求

请优先读取并使用当前目录中的以下资料：

1. `generation_log.json`
2. `acceptance_manifest.json`
3. `build_log.txt`
4. UE 输出日志，例如 `Saved/Logs/*.log`
5. 相关蓝图的 expected_ir JSON
6. 相关蓝图的 DOT / Mermaid
7. `coverage_matrix.md`
8. `manual_check_guide.md`
9. `regression_protocol.md`
10. 相关生成器源码
11. 相关解析器源码
12. 本地验收脚本
13. 我提供的截图或报错文本

如果 shell 不可用，也必须尽可能通过文件读取和静态分析完成诊断，不要直接终止。

# 五、诊断流程

请按以下步骤处理。

## 阶段 1：问题复述与证据整理

先输出：

```text
# 问题复述

## 1. 出问题的资产
说明蓝图或支持资产名称。

## 2. 出问题的位置
说明节点、Pin、Graph、连线或文件位置。

## 3. 观察到的现象
整理 UE 报错、警告、截图描述、日志信息。

## 4. 当前可用证据
列出已读取的日志、JSON、源码、报告、截图等。

## 5. 初步问题分类
按问题类型归类。
```

## 阶段 2：根因分析

你需要输出：

```text
# 根因分析

## 1. 直接原因
说明当前蓝图为什么报错或警告。

## 2. 生成器层原因
分析生成器哪里做错了，或哪里缺少通用处理。

## 3. 解析器层原因
如果涉及解析结果不一致，分析解析器哪里需要改。

## 4. expected_ir / 可视化 / 文档层原因
如果报告与实际不一致，说明哪里需要同步。

## 5. UE 版本 / API 差异因素
说明是否与 UE 版本有关。

## 6. 同类问题影响范围
列出可能受影响的节点类型、Pin 类型、Graph 类型和其他蓝图。
```

请注意：如果根因不止一个，必须分层说明，不要只给单一原因。

## 阶段 3：通用修复设计

你必须先设计通用修复方案，再动手修改。

请输出：

```text
# 通用修复设计

## 1. 修复目标
说明不仅要修当前问题，还要修哪一类通用问题。

## 2. 修复原则
说明如何避免硬编码当前蓝图或当前工程。

## 3. 需要修改的模块
列出生成器、解析器、校验器、脚本、expected_ir、可视化、文档等。

## 4. 通用抽象
例如新增：
- Pin 查找与校验工具
- 强类型连接工具
- Wildcard 类型稳定化工具
- Macro 解析兼容层
- Delegate 绑定构建器
- Struct/Enum 刷新与重建流程
- Node Reconstruct/Refresh 工具
- Graph consistency checker
- expected_ir 结构校验器

## 5. 同类节点覆盖范围
说明本次修复会同时覆盖哪些节点，而不只是当前节点。
```

# 六、必须优先完善的通用能力

遇到实际问题时，你应该尽量把修复沉淀到以下通用能力中。

## 1. Pin 查找与连接工具

如果出现缺连线、Pin 找不到、Pin 名称不同、Pin 类型不对等问题，请优先完善统一工具，而不是在单个节点里写死。

需要支持：

- 按精确 Pin 名查找。
- 按候选 Pin 名查找。
- 按 Direction 查找。
- 按 PinCategory 查找。
- 按 PinSubCategoryObject 查找。
- 按容器类型查找。
- 对 UE 版本差异提供候选名。
- 找不到 Pin 时写入结构化错误。
- 连接前校验 Pin 类型。
- 连接后校验 Link 是否真的建立。
- 对 Wildcard Pin 提供强制类型化流程。

## 2. 节点生命周期工具

如果出现节点红掉、Pin 没生成、节点签名不刷新，请完善节点生命周期流程。

统一封装：

- 创建节点
- 设置 FunctionReference / MacroReference / DelegateReference
- `PostPlacedNewNode`
- `AllocateDefaultPins`
- `ReconstructNode`
- `NodeConnectionListChanged`
- `MarkBlueprintAsStructurallyModified`
- Compile Blueprint
- Save Package
- 记录 warning / error

## 3. Macro 兼容层

如果 StandardMacros 出问题，请不要只修 DoOnce 或 ForLoop。

需要建立统一 Macro 工具：

- 通过 StandardMacros 资产路径查找宏库。
- 校验宏名是否存在。
- 支持 DoOnce、DoN、FlipFlop、Gate、ForLoop、ForLoopWithBreak、ForEachLoop、ForEachLoopWithBreak、WhileLoop。
- 每个宏实例创建后检查关键 Pin 是否存在。
- 如果宏名或 Pin 名在 UE 版本间变化，输出兼容映射。
- 如果无法自动创建，标记为“需要人工确认”，不要伪造成功。

## 4. Delegate / Event Dispatcher 构建器

如果委托绑定出问题，请建立统一 Dispatcher 构建流程：

- 创建 Event Dispatcher。
- 创建签名图。
- 创建 Custom Event。
- 确保 Custom Event 参数与 Dispatcher 参数一致。
- 创建 CreateDelegate。
- 设置绑定函数。
- 创建 AddDelegate / ClearDelegate / CallDelegate。
- 校验 delegate pin 连接。
- 校验 CallDelegate 参数 Pin。
- 校验 expected_ir 中的 delegate edge。

## 5. Wildcard 容器节点稳定化

如果 Array / Set / Map 出问题，请统一处理：

- 先创建强类型变量。
- 用变量 Get Pin 约束 wildcard 节点。
- 再设置 item/key/value Pin 默认值或连接。
- 连接后重新 Reconstruct。
- 验证 PinCategory / ContainerType。
- 覆盖 Array Add/Get/Remove/Length、Set Add/Remove/Contains、Map Add/Find/Remove/Keys/Values、MakeArray/MakeSet/MakeMap。

## 6. Struct / Enum / Interface 刷新流程

如果自定义类型相关节点出问题，请统一处理：

- 创建 Struct / Enum / Interface 后立即保存。
- 刷新 AssetRegistry。
- 编译相关 Blueprint。
- 在 MakeStruct / BreakStruct / SwitchEnum 前确保类型已加载。
- 字段 Pin 不能只按显示名盲连，要校验字段实际存在。
- Enum 显示名与内部名要分开处理。

## 7. expected_ir 与实际生成结构一致性校验

如果 UE 实际蓝图和 expected_ir 不一致，请完善校验器：

- expected_ir 不是理想草图，而应尽量对应生成器实际逻辑。
- 每个生成器创建的节点都应有 expected_ir 映射。
- 每条关键连线都应有 edge。
- 如果引擎自动生成隐藏 Pin 或宏展开节点，应在差异规则里标为 info。
- 如果生成器未生成但 expected_ir 写了，必须修正其中一方。

# 七、修复执行要求

完成通用设计后，请执行修复。

每个修改必须说明：

1. 修改文件。
2. 修改原因。
3. 修复的问题类型。
4. 是否影响其他蓝图。
5. 是否新增回归检查。
6. 是否需要更新 expected_ir。
7. 是否需要更新 DOT / Mermaid。
8. 是否需要更新覆盖矩阵。
9. 是否需要更新人工检查指南。
10. 是否需要重新运行本地验收脚本。

如果能直接修复代码，请直接修改。
如果无法直接修改，请输出完整补丁，不要只给片段。

# 八、重新验证要求

修复后必须重新验证，不允许修完就结束。

请至少执行或准备执行：

1. 重新编译插件。
2. 重新运行生成器。
3. 重新检查 `generation_log.json`。
4. 重新检查相关蓝图是否生成。
5. 重新检查 expected_ir JSON 合法性。
6. 重新检查 DOT / Mermaid。
7. 更新 `acceptance_manifest.json`。
8. 更新 `failed_items.json`。
9. 更新 `manual_check_required.json`。
10. 输出本次修复后的本地验收结果。

如果当前环境无法实际运行 UE，请明确标记为：

```text
代码级修复已完成，但 UE 本地验证待用户执行。
```

并给出我本地需要运行的命令。

# 九、输出机器可读修复报告

为了让 Claude Code CLI 或其他 Agent 能调用并理解本次修复结果，请生成或更新：

```
Saved/BPParserTestReports/fix_report_<timestamp>.json
```

格式建议：

```json
{
  "schema_version": "1.0",
  "issue": {
    "asset": "",
    "graph": "",
    "node": "",
    "pin": "",
    "symptom": "",
    "ue_message": ""
  },
  "classification": [
    "missing_data_edge",
    "wildcard_type_inference_failure"
  ],
  "root_cause": {
    "direct": "",
    "generator_layer": "",
    "parser_layer": "",
    "ir_layer": "",
    "version_compatibility": ""
  },
  "affected_patterns": [
    {
      "pattern": "Wildcard container function",
      "affected_nodes": ["Array_Add", "Array_Get", "Map_Find"],
      "risk": "high"
    }
  ],
  "fix": {
    "strategy": "",
    "modified_files": [],
    "new_files": [],
    "deleted_files": [],
    "generalized_components": [],
    "hardcoded_workarounds": []
  },
  "validation": {
    "build_rerun": "success|failed|skipped",
    "generate_rerun": "success|failed|skipped",
    "json_validation": "success|failed|skipped",
    "ue_manual_check_required": []
  },
  "next_user_action": []
}
```

# 十、更新知识库 / 问题模式库

每次修复后，请维护一个问题模式库，例如：

```
docs/issue_patterns.md
```

每条记录包含：

```markdown
## 问题模式：Wildcard 容器节点类型未稳定

### 典型现象
Array_Add / Map_Find 节点红色，Item/Key/Value Pin 类型不对，Compile 报 wildcard 未解析。

### 常见触发节点
Array_Add、Array_Get、Set_Add、Map_Add、Map_Find、MakeArray、MakeMap。

### 根因
节点创建后没有先通过强类型容器变量约束 wildcard pin，或连接顺序错误。

### 通用修复
先创建强类型变量 Get，连接 TargetArray/TargetMap，再 ReconstructNode，然后设置 item/key/value。

### 验证方法
检查 PinCategory、ContainerType、LinkedTo，重新编译蓝图，检查 generation_log。

### 回归用例
BP_02_StructEnumContainers、BP_11_SupplementalCoverage。
```

不要只把经验留在对话里，必须沉淀到文档和代码结构中。

# 十一、最终输出格式

最后请输出：

```text
# 本地验收问题修复报告

## 1. 问题现象
说明我在 UE 中看到的问题。

## 2. 问题分类
说明这属于哪类蓝图生成 / 解析 / IR / 可视化问题。

## 3. 根因分析
分层说明直接原因、生成器原因、解析器原因、UE 版本原因。

## 4. 通用影响范围
说明同类问题可能影响哪些节点、Pin、Graph 和蓝图。

## 5. 通用修复方案
说明不是如何修当前特例，而是如何修这一类问题。

## 6. 已修改文件
列出所有改动。

## 7. 已新增回归用例或校验规则
说明如何防止同类问题复发。

## 8. 更新的文档和报告
说明 coverage、manual guide、issue_patterns、expected_ir、viz 是否更新。

## 9. 重新验证结果
说明编译、生成、JSON 校验、可视化校验是否通过。

## 10. 仍需我在 UE 中人工确认的内容
列出具体蓝图、节点、Pin 或截图要求。
```

# 十二、禁止事项

1. 不要只修当前节点。
2. 不要只改 expected_ir 让它“看起来一致”，而不修生成器根因。
3. 不要只改文档不改代码。
4. 不要把 UE 本地编译失败伪造成插件成功。
5. 不要硬编码当前蓝图名来绕过问题。
6. 不要忽略同类型节点。
7. 不要把缺少连线简单归因于“忘了连”，必须解释为什么生成器会漏连。
8. 不要让 coverage_matrix 声称已覆盖但实际生成器没有创建。
9. 不要忽略 Agent / CLI 可调用性。
10. 不要省略机器可读 fix_report。