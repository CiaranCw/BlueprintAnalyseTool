# 架构总览

## 1. 总体数据流

```text
                ┌────────────────────────────────────────────┐
                │            UE Editor Process               │
                │  (UnrealEditor-Cmd.exe -run=BPATDump ...)   │
                │                                            │
   AssetPath ─►│  AssetEnumerator ─► BlueprintLoader        │
                │        │                  │                │
                │        ▼                  ▼                │
                │  AssetInfoReader   GraphEnumerator         │
                │        │                  │                │
                │        ▼                  ▼                │
                │  MemberReader       NodeReader             │
                │  ComponentTreeR.    PinReader              │
                │  WidgetTreeR.       EdgeResolver           │
                │  AnimGraphR.                               │
                │        │                  │                │
                │        └────────┬─────────┘                │
                │                 ▼                          │
                │            IRBuilder                       │
                │                 │                          │
                │                 ▼                          │
                │         IRValidator (结构 + Schema)         │
                │                 │                          │
                │                 ▼                          │
                │            IRSerializer ─► OutputDir/      │
                │                                manifest    │
                │                                graphs/*    │
                │                                nodes/*     │
                │       ReadOnlyGuard 全程监听副作用          │
                └────────────────────────────────────────────┘
                                  │
                                  ▼
                ┌────────────────────────────────────────────┐
                │       Agent Side (Python, 无 UE)            │
                │                                            │
                │   IRClient ─► Indexer ─► QueryAPI ─► Agent │
                │                  │           │             │
                │                  ▼           ▼             │
                │              Slicer   SchemaValidator      │
                └────────────────────────────────────────────┘
```

UE 侧负责把蓝图变成 IR（**有副作用层**：写 `OutputDir`）。
Agent 侧只读 IR + 索引（**无副作用层**：除非显式重建索引）。

---

## 2. 模块边界

| 进程 / 语言     | 模块                                    | 责任                              | 是否需要 UE | 写文件                |
| --------------- | --------------------------------------- | --------------------------------- | ----------- | --------------------- |
| UE Editor / C++ | `BlueprintAgentToolsEditor`             | 解析蓝图 → IR → 落盘              | 是          | 仅 `OutputDir`        |
| Python          | `blueprint_agent_tools.query_api`       | Agent 顶层查询入口                | 否          | 否（默认）            |
| Python          | `blueprint_agent_tools.indexer`         | 基于 IR 构建索引                  | 否          | `OutputDir/_index/`   |
| Python          | `blueprint_agent_tools.slicer`          | 子图切片                          | 否          | 可选缓存到 `slices/`  |
| Python          | `blueprint_agent_tools.schema_validator`| JSON Schema 校验                  | 否          | 否                    |
| Python          | `blueprint_agent_tools.ue_runner`       | 调起 UE Commandlet                | 间接        | 否（由 UE 进程写）    |

---

## 3. 调用顺序（单蓝图）

详见 `docs/atomic_capabilities.md` 第 4 节。

简化流：

```text
ParseArgs → OutputDirManager.Validate
          → ReadOnlyGuard.Begin
          → AssetEnumerator.Lookup(AssetPath)
          → BlueprintLoader.Load
          → AssetInfoReader / MemberReader / ComponentTreeReader
          → (Type Dispatch) WidgetTreeReader | AnimGraphReader
          → GraphEnumerator
              for each Graph:
                  NodeReader → PinReader → EdgeResolver
          → IRBuilder.Build
          → IRValidator.Validate
          → IRSerializer.WriteAll
          → ReadOnlyGuard.End
```

---

## 4. 输出目录布局

```text
<OutputDir>/<ProjectName>/
├─ _summary.json                 仅批量模式有
├─ _index/                       Python 索引器产物
│  ├─ assets.sqlite
│  ├─ nodes.sqlite
│  └─ search.json
├─ logs/
│  └─ <safe_path>.jsonl
└─ blueprints/
   └─ <safe_path>/
      ├─ manifest.json
      ├─ graphs/
      │  ├─ <graph_id>.summary.json
      │  └─ <graph_id>.full.json   小图直接 inline
      ├─ nodes/
      │  └─ <node_id>.json         大图按需拆分
      └─ slices/
         └─ <slice_hash>.json
```

`<safe_path>` = 由 `/Game/Blueprints/BP_Hero` 转成 `Game__Blueprints__BP_Hero`。

---

## 5. 错误码

详见 `docs/agent_tools.md` 第 4 节。退出码与错误码一一对应：

| 退出码 | 含义                                       |
| ------ | ------------------------------------------ |
| 0      | 全部成功                                   |
| 10     | partial（IR 已写，有 warning）             |
| 20     | 至少一个 fatal（IR 未写），其它成功        |
| 30     | 参数 / 输出目录非法                        |
| 40     | 检测到只读违规（已中止）                   |
| 50     | UE 加载或 Commandlet 内部异常              |

---

## 6. 日志规范

- UE 侧：`Log` 级日志走 `BPAT` 类目（`UE_LOG(LogBPAT, ...)`）。
- 结构化日志：`OutputDir/logs/<safe_path>.jsonl`，每行一个事件：
  ```json
  {"ts":"2026-05-19T08:00:00Z","stage":"NodeReader","level":"info","node_count":42}
  ```
- ReadOnlyGuard 触发时，写一条 `level=fatal` 并以退出码 40 退出。

---

## 7. 待验证项（已通过文档级验证，现场 spike 待跑）

下表"现状"列汇总了对 UE 5.4 / 5.x 公开文档与源码注释的检索结论。每项**仍需现场 spike** 在目标工程上跑一次确认，spike 源文件见 `ue_plugin/.../Private/Spike/BPATSpikeCommandlet.cpp`，计划与预期输出见 `docs/spike_ue54.md`。

| #  | 项                                                              | 现状（文档级置信度）                                                                                  | 影响                                       |
| -- | --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| V1 | UE 5.4 中 Save 钩子精确名                                       | **HIGH**：`UPackage::PackageMarkedDirtyEvent` (`FOnPackageMarkedDirty`) 与 `UPackage::PackageSavedWithContextEvent` (`FOnPackageSavedWithContext`) 在 UE 5.x 公开 API 中稳定存在，定义于 `UObject/Package.h`。同时存在 `PreSavePackageWithContextEvent`，可叠加用于"保存前抢拦"。 | `BPATReadOnlyGuard` 实现                   |
| V2 | `IAssetRegistry::GetAssetsByClass` 是否需要 `FTopLevelAssetPath` | **HIGH**：UE 5.1+ 起 `FName` 版本被弃用；5.4 必须用 `FARFilter::ClassPaths`（`TArray<FTopLevelAssetPath>`）。Blueprint 类路径示例：`FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"))`。 | `BPATAssetEnumerator` 实现                 |
| V3 | Latent 节点判定                                                 | **HIGH**：`Latent` 是合法 UFUNCTION metadata；判定方式：`Cast<UK2Node_CallFunction>(Node)->GetTargetFunction()->HasMetaData(TEXT("Latent"))`。非 CallFunction 的 latent 行为（自定义节点）无通用 API，按节点类白名单识别（如 `UK2Node_BaseAsyncTask`、`UK2Node_AsyncAction`）。 | `BPATEdgeResolver` 边类型分类              |
| V4 | Commandlet 中并发加载多个 Blueprint 是否安全                    | **HIGH 否**：UE 文档明确 UObject 创建在多线程下默认不安全；存在 GC 死锁风险，需要 `FGCScopeGuard` 配合。Epic 官方 `CompileAllBlueprintsCommandlet` 是串行的。**结论：当前阶段串行**。 | `BPATDumpCommandlet` 默认串行              |
| V5 | UWidgetBlueprint::WidgetTree 在 5.4 暴露形式                    | **HIGH**：`UWidgetBlueprint` 的 `WidgetTree` 成员（`UWidgetTree*`）跨 5.x 稳定。迭代用 `UWidgetTree::ForEachWidgetAndDescendants(TFunctionRef<void(UWidget*)>)`。同时有 `RootWidget`、`AllWidgets`、`NamedSlotBindings`。 | `BPATWidgetTreeReader`                     |
| V6 | UAnimBlueprint AnimGraph 类层级                                 | **HIGH**：`UAnimBlueprint : public UBlueprint`。AnimGraph 节点全部继承 `UAnimGraphNode_Base`（继承 `UK2Node`，模块 `AnimGraph`）。AnimGraph 本身是 `Blueprint->FunctionGraphs` 中名为 `"AnimGraph"` 的图（5.4 仍如此）；可用 `Cast<UAnimGraphNode_Base>(Node) != nullptr` 判定。 | `BPATAnimGraphReader`                      |
| V7 | Level Blueprint 加载副作用                                      | **MEDIUM 重副作用**：访问 `ULevelScriptBlueprint` 必须先把 umap 加载为 `UWorld`。Epic 路径需要 `UEditorLoadingAndSavingUtils::LoadMap` 或 `LoadPackage(<umap>)`，会拉起整张 Level + Actor 实例化。**结论：默认不进入批量流程**，仅在显式 `-LevelMaps=...` 时启用。 | Level Blueprint partial，默认不批量        |
| V8 | 加载 UBlueprint 时 PostLoad 是否触发隐式编译                    | **HIGH 是**：`FBlueprintCompilationManager::NotifyBlueprintLoaded` 会被 PostLoad 触发并入队编译。Epic 官方做法（`CompileAllBlueprintsCommandlet`）使用 `LOAD_NoWarn \| LOAD_DisableCompileOnLoad`，**这正是我们要采用的旗标**。当前阶段所有 `BPATBlueprintLoader::Load` 都加这两个 LoadFlags。 | `BPATBlueprintLoader` 必加 LoadFlags       |

> 现场 spike 跑完后，请在每项右侧追加 `[已现场验证 - YYYY-MM-DD]`，并在 `docs/spike_ue54.md` 末尾贴一份原始 log 摘录。

### 7.1 由验证结论确定的关键决策

1. **加载蓝图时强制使用** `LOAD_NoWarn | LOAD_DisableCompileOnLoad`（V8）。
2. **默认串行**：`BPATDumpCommandlet` 不启用并发加载（V4）。
3. **AssetRegistry 过滤一律用** `FARFilter::ClassPaths` + `FTopLevelAssetPath`（V2）。
4. **ReadOnlyGuard 同时订阅** `PackageMarkedDirtyEvent` 与 `PackageSavedWithContextEvent`（V1），并预留对 `PreSavePackageWithContextEvent` 的早期抢拦扩展点。
5. **AnimGraph 节点识别**用 `IsA<UAnimGraphNode_Base>()` 而非节点名匹配（V6）。
6. **Widget 树**统一通过 `UWidgetTree::ForEachWidgetAndDescendants` 迭代（V5）。
7. **Level Blueprint 默认 partial 且不批量**，仅在显式 map 列表参数下启用（V7）。
8. **Latent 边判定**：仅对 `UK2Node_CallFunction` 走 `HasMetaData(TEXT("Latent"))`；其它继承 `UK2Node_BaseAsyncTask` 的节点也按白名单标 latent；其余 fallback 到 `exec` 加 warning（V3）。
