# UE 5.4 现场 Spike 计划

文档级验证（见 `architecture.md` §7）已经把每个待验证项的 API 形态确定到 HIGH 置信度，但仍需在你本机 UE 5.4 + 真实工程上跑一次以确认：
- 这些 API 在你的引擎构建中确实存在并签名匹配
- 我们对副作用（PostLoad / GC / SavePackage）的假设在你的工程上成立
- AnimBlueprint / WidgetBlueprint 在你工程的实际版本上结构未被定制

本文档配合 `ue_plugin/.../Private/Spike/BPATSpikeCommandlet.cpp` 一起使用。

---

## 0. Spike 准备

1. 把整个 `ue_plugin/BlueprintAgentTools/` 复制到 **一个临时 UE 5.4 测试工程**（不要用真实生产工程）的 `Plugins/` 下。
2. 在 `MyTestProject.uproject` 中开启 `BlueprintAgentTools` 插件。
3. 在测试工程里准备 fixture 资产：
   - 1 个普通 Actor BP（含 EventGraph、1 个自定义函数图、1 个 SCS 组件树）
   - 1 个 WidgetBlueprint（含至少 1 层 NamedSlot 嵌套）
   - 1 个 AnimBlueprint（含一个 StateMachine）
   - 1 个 Level（不要求复杂）
4. 重新编译插件。Spike 命令：

```powershell
UnrealEditor-Cmd.exe MyTestProject.uproject -run=BPATSpike -OutputDir=D:\bpat_spike
```

> Spike 的 OutputDir 也必须在工程外。Spike 命令同样受 `BPATPathPolicy` 保护。

---

## 1. V1 — Save 钩子

**预期**：Spike 中我们注册 `UPackage::PackageMarkedDirtyEvent` 和 `UPackage::PackageSavedWithContextEvent`。然后**只读加载**一个蓝图，期间不应观察到任何被保护包路径触发上述事件。

**通过条件**：
- 二者均能成功 `AddLambda(...)` 并取回 `FDelegateHandle`（编译通过）
- 整个 spike 跑完，`OutputDir/spike/v1_dirty_packages.json` 与 `v1_saved_packages.json` 都为空数组
- 故意触发：在 spike 末尾对一个**测试用临时包**（不是 `/Game/`）调用 `MarkPackageDirty`，期望 dirty 事件**触发**，并且因为不是 `/Game/` 前缀**不会**记录到 violation 列表。

**失败处理**：若 `PackageSavedWithContextEvent` 在你的引擎构建里换名（罕见），改用 `FCoreUObjectDelegates::OnPackageSavedWithContext`。这是同一份事件的另一份对外门面。

---

## 2. V2 — AssetRegistry FTopLevelAssetPath

**预期**：用以下代码枚举工程内 Blueprint：

```cpp
FARFilter Filter;
Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMG"), TEXT("WidgetBlueprint")));
Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));
Filter.PackagePaths.Add(FName(TEXT("/Game")));
Filter.bRecursivePaths = true;

TArray<FAssetData> Assets;
IAssetRegistry::GetChecked().GetAssets(Filter, Assets);
```

**通过条件**：
- 编译通过（FName-based `GetAssetsByClass` 重载在 5.4 已弃用）
- `Assets.Num() > 0`，且每个 `FAssetData::AssetClassPath` 是 `FTopLevelAssetPath`
- 写入 `OutputDir/spike/v2_assets.json` 时能看到 fixture 蓝图的路径

**失败处理**：若 `IAssetRegistry::GetChecked()` 不存在，改用 `FAssetRegistryModule::Get().Get()`。两者最终都是 `IAssetRegistry&`。

---

## 3. V3 — Latent metadata

**预期**：spike 加载一个**已知调用了 Delay 节点**的 fixture BP。遍历其 EventGraph 的所有节点：

```cpp
for (UEdGraphNode* Node : EventGraph->Nodes)
{
    if (auto* CallNode = Cast<UK2Node_CallFunction>(Node))
    {
        if (UFunction* F = CallNode->GetTargetFunction())
        {
            const bool bIsLatent = F->HasMetaData(TEXT("Latent"));
            // record
        }
    }
}
```

**通过条件**：
- `Delay` / `MoveComponentTo` / `OnlineSubsystem*` 等 latent 节点 `bIsLatent == true`
- `PrintString` / `Add` 等普通节点 `bIsLatent == false`
- 输出 `OutputDir/spike/v3_latent_classification.json`

**失败处理**：如果某些项目自定义了非 K2Node_CallFunction 的 latent 节点，我们用类白名单：`UK2Node_BaseAsyncTask`、`UK2Node_AsyncAction`、`UK2Node_BaseMCDelegate` 派生类（待逐一确认）。

---

## 4. V4 — 串行加载（只验证不并发）

**预期**：spike 串行加载 N 个蓝图，记录每个的 `LoadElapsedMs`。**不**尝试并发——本项 spike 的目的只是**确认**串行能稳定跑完，且整个跑完没有 GC 报告 fatal。

**通过条件**：
- N=20 个蓝图全部成功 Load，进程不崩溃
- 跑完后 `IsGarbageCollecting() == false`
- 若 N 较大（比如 200+），观察峰值内存稳定，不出现明显泄漏

**失败处理**：N 大时若内存暴涨，spike 末尾插入一次 `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, false)`，验证可回收。

---

## 5. V5 — WidgetTree 迭代

**预期**：

```cpp
UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *PackagePath);
if (UWidgetTree* Tree = WBP->WidgetTree)
{
    Tree->ForEachWidgetAndDescendants([](UWidget* W)
    {
        // record W->GetClass()->GetPathName(), W->GetName()
    });
}
```

**通过条件**：
- 能拿到 `WidgetTree`，且非空
- 列出的 widget 数量 == 你 fixture WidgetBP 的实际控件数
- NamedSlot 嵌套的子控件被 ForEachWidgetAndDescendants 遍历到

---

## 6. V6 — AnimBlueprint 顶层结构

**预期**：

```cpp
UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *PackagePath);
for (UEdGraph* G : AnimBP->FunctionGraphs)
{
    int32 AnimNodeCount = 0;
    for (UEdGraphNode* N : G->Nodes)
    {
        if (Cast<UAnimGraphNode_Base>(N))
        {
            ++AnimNodeCount;
        }
    }
    // 输出 G->GetFName(), AnimNodeCount
}
```

**通过条件**：
- 至少一个图 `AnimNodeCount > 0`，对应 `AnimGraph`
- StateMachine 节点（`UAnimGraphNode_StateMachineBase`）被识别为 `UAnimGraphNode_Base` 子类

**失败处理**：若 `Cast<UAnimGraphNode_Base>` 失败，可能是依赖模块名不正确。检查 `BlueprintAgentToolsEditor.Build.cs` 是否包含 `AnimGraph` 模块（已包含）。

---

## 7. V7 — Level Blueprint（验证副作用范围）

**预期**：spike 在 `-LevelMaps=/Game/Maps/M_Test` **显式启用**时才尝试加载 umap。加载前后 snapshot Project 文件指纹（`scripts/verify_readonly.py`）。

**通过条件**：
- 在不指定 `-LevelMaps` 时，spike 完全跳过 Level Blueprint，不调 `LoadMap`
- 在指定 `-LevelMaps` 时，加载完毕后工程文件指纹**不变**（即 LoadMap 本身不写盘，但加载链很重）
- 能成功通过 `World->PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate=*/true)` 取到 `ULevelScriptBlueprint*`

**结论方向**：默认批量模式不带 Level Blueprint；Agent 用 `dump_level_blueprint(map_path)` 显式触发（接口未来再加）。

---

## 8. V8 — PostLoad 隐式编译抑制

**预期**：用两组对照 spike：

```cpp
// 组 A（不抑制）：观察是否 dirty
UBlueprint* A = LoadObject<UBlueprint>(nullptr, *PackagePath);

// 组 B（抑制）：使用 LOAD_NoWarn | LOAD_DisableCompileOnLoad
UBlueprint* B = LoadObject<UBlueprint>(nullptr, *PackagePath, nullptr,
                                       LOAD_NoWarn | LOAD_DisableCompileOnLoad);
```

**通过条件**：
- 组 A 在某些 BP 上可能触发 dirty（取决于工程版本兼容性）
- 组 B **绝对不**触发任何 `/Game/` dirty 事件
- 最终决议：`BPATBlueprintLoader::Load` **始终**使用组 B 的 LoadFlags

**这个 spike 的意义**：把"加载蓝图绝对只读"从直觉变成**有 log 证据**的硬约束。

---

## 9. Spike 报告产出

跑完 spike 后，把以下文件存档（可作为 PR 的附件）：

```text
OutputDir/spike/
├─ v1_dirty_packages.json
├─ v1_saved_packages.json
├─ v2_assets.json
├─ v3_latent_classification.json
├─ v4_load_stats.json
├─ v5_widget_tree.json
├─ v6_anim_graph.json
├─ v7_level_blueprint.json   (仅当显式启用)
├─ v8_load_dirty_compare.json
└─ summary.json              全部通过/失败的汇总
```

把 `summary.json` 的关键字段贴回 `docs/architecture.md` §7 表格右侧 `[已现场验证 - YYYY-MM-DD]`。

---

## 10. 通过标准（M1 启动门槛）

- V1 / V2 / V8 必须 100% 通过；任一失败 → 当前阶段架构需重做。
- V3 / V5 / V6 失败 → 对应 reader 降级为 partial，但 M1 仍可启动（避开该 reader）。
- V4 失败（GC 异常 / 崩溃）→ 减小 fixture 规模重试；如仍不通过 → 暂禁批量模式。
- V7 默认绕过；只在确实需要时再做。
