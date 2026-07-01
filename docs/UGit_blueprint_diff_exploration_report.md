# UGit 集成 UE 蓝图结构可视化 Diff 可行性探索报告

> 本报告基于**本地实际源码/文件**调查，不是凭空假设。UE 源码路径以 `D:\software\UE\UE_5.4\Engine\Source` 为准；UGit 以本地安装目录 `C:\Users\test\AppData\Local\UGit`（Electron 打包产物，`app-5.50.0\resources\app` 为**已解包**的应用代码，含 source map，可读）为准。凡引用均给出文件与行号/类名。
>
> 说明（重要前提）：本次能查到的是 UGit 的**发行/解包产物**（webpack 打包后的 `main.js`/`renderer.js` 等，可读且带 `.map`），**没有拿到 UGit 的源码工程**。这足以判断可行性、扩展点与技术栈；但真正落地 PoC 时，改动应在 UGit 源码工程中进行（或对打包产物做一次性 patch 做演示）。

---

## 1. 结论摘要

- **能不能做：能。** 技术上完全可行，且比预期更顺——UE 与 UGit 两侧都已具备关键基础设施。
- **推荐主线：路线 A（UGit 调用 UE Worker 导出 IR + Diff，UGit 内 WebView 可视化）。**
- **强力增强：路线 D（UE 侧原生截图）**作为"像素级参照图"叠加，可选后置。
- **不推荐主线：路线 B（UGit 直接链接 UE Editor 模块）**、**路线 C（完全外部解析 .uasset 做图结构 Diff）**。
- **PoC 优先级：高。技术阻塞：无。** 最大风险是**工程/引擎环境依赖**（需要可用的 UE 工程 + 引擎来加载资产），而非算法或 UI。
- **可复用的 UE 能力（关键）**：
  - `DiffUtils::LoadPackageForDiff(...)`（`Editor/UnrealEd/Private/DiffUtils.cpp:1180`）——用 `LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks` 把某个版本加载进**临时包**，**天然解决 old/new 同名包冲突**且跳过编译。
  - `FGraphDiffControl::DiffGraphs / FindNodeMatch / IsNodeMatch / FNodeMatch::Diff`（`Editor/GraphEditor/Public/GraphDiffControl.h`）——**无 UI 依赖**的图级 Diff + 节点匹配，正是蓝图 Diff 工具背后的逻辑。
  - `FDiffSingleResult` / `EDiffType`（`Runtime/Engine/Public/DiffResults.h`）——现成的 Diff 语义模型（`NODE_ADDED/REMOVED/MOVED`、`NODE_COMMENT`、`PIN_DEFAULT_VALUE`、`PIN_TYPE_*`、`PIN_LINKEDTO_*`、`NODE_PIN_COUNT`…）。
  - 本仓库已验证可用的 `FBPGenIRDumper`（`bpparser_testgen/.../BPGenIRDumper.cpp`）——已在 headless commandlet 中稳定导出 NodeGuid/pins/edges/comments/positions 的 IR JSON。
- **必须自研的能力**：稳定 `stable_id` 生成、IR↔Diff 的对齐与序列化、UGit 侧 `DiffType.Blueprint` 视图与画布渲染、blob→临时文件→Worker 的编排与缓存。
- **UGit 侧接入点（关键）**：`DiffType` 枚举 + 按扩展名构造 diff 对象的分发点（`renderer.js` 约 L193273 处 `imageFileExtensions.has(extension)` → `kind: DiffType.Image` 的同类分支），新增 `.uasset → DiffType.Blueprint` 分支 + 对应 React 视图组件；外部 Worker 复用其已有的 `UExcelDiff.exe` 模式。

---

## 2. 本地源码调查结果

### 2.1 UE 侧相关模块（实际找到的位置）

| 关注点 | 实际文件 | 说明 |
|---|---|---|
| 图级 Diff 算法 | `Editor/GraphEditor/Public/GraphDiffControl.h` + `Private/GraphDiffControl.cpp` | `FGraphDiffControl::DiffGraphs(OldGraph, NewGraph, TArray<FDiffSingleResult>&)`，静态、纯数据、可 headless |
| Diff 结果模型 | `Runtime/Engine/Public/DiffResults.h` | `EDiffType`、`FDiffSingleResult`、`FDiffResults`；**注意** 头文件 `#include "Styling/AppStyle.h"`（`GetDisplayColor()` 依赖 Slate）→ 需在**加载了 Slate 的 commandlet**里用，不能放进纯 non-editor exe |
| 蓝图 Diff UI | `Editor/Kismet/Public/SBlueprintDiff.h` + `Private/SBlueprintDiff.cpp`、`Private/DiffControl.cpp` | 编辑器内的可视化 Diff 面板（Slate UI，**依赖 Editor UI**，不能 headless 直接复用其"界面"） |
| 合并视图 | `Developer/Merge/Private/SMergeGraphView.cpp`、`SBlueprintMerge.h` | 三方合并 UI |
| 资产加载(Diff) | `Editor/UnrealEd/Private/DiffUtils.cpp` | `DiffUtils::LoadPackageForDiff`（临时包 + `LOAD_ForDiff`），另有从 `ISourceControlRevision` 载入的重载 |
| 现成 headless 命令 | `Editor/UnrealEd/Private/Commandlets/DiffAssetsCommandlet.cpp`（`-run=DiffAssets`）、`DiffFilesCommandlet.cpp` | **但只导出 t3d 文本再交给外部文本 diff**，非结构化图 Diff |
| 节点/Pin 数据 | `Runtime/Engine/Classes/EdGraph/EdGraphNode.h`、`EdGraph/EdGraphPin.h` | 见 2.3 |

### 2.2 UE 内部蓝图 Diff 能力（回答 prompt 关键问题）

- **UE 内部蓝图 Diff 如何实现？** UI 层是 `SBlueprintDiff`（Kismet），它对每一对同名 Graph 调用 `FGraphDiffControl::DiffGraphs` 得到 `FDiffSingleResult[]`，再把结果渲染成差异树 + 两个只读 GraphEditor 面板并高亮。
- **是否依赖 Editor UI？** **算法不依赖，UI 依赖。** `FGraphDiffControl`/`FDiffSingleResult` 是数据层；只有"展示"绑定 Slate。所以我们**复用算法、放弃它的 UI**。
- **能否在命令行/Headless 加载蓝图并导出结构？** 能。`UnrealEditor-Cmd.exe <proj> -run=<Commandlet>` 会加载 Editor 模块（GraphEditor/Kismet 等），`LoadPackage` + 遍历 `UBlueprint::UbergraphPages/FunctionGraphs/MacroGraphs` 即可。本仓库 `BPParserTestGen` 插件的 `BPParserTestDump`/`BPATEdit` 两个 commandlet 已在 5.4 实测跑通，`FBPGenIRDumper` 已导出完整 IR。
- **能否复用 `FGraphDiffControl`？** **能，且推荐。** 它把节点匹配（`FindNodeMatch`）与逐节点比较（`FNodeMatch::Diff`）都封装好了，直接产出 `EDiffType` 级别的差异。
- **不能直接复用的部分及原因：** `SBlueprintDiff` 的**可视化**（Slate 控件、GraphEditor 面板）无法搬进 UGit（Electron/Web 技术栈，且 Slate 属 Editor-only、体积与授权都不适合外链）。
- **old/new 同名 `.uasset` 能否同进程同时加载？如何规避包名冲突？** 这是最关键的工程点，UE 已给出标准答案：`DiffUtils::LoadPackageForDiff`（`DiffUtils.cpp:1180`）把某一版本从**临时文件**载入**临时包名**（`/Temp/...`，`FPackageName::IsTempPackage`），使用 `LoadPackage(nullptr, *TempPackagePath, LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks, ...)`。因此：**把 old blob 落到临时文件、以临时包名载入；new 用工作区 `/Game/...` 正常载入（或同样临时包）**，二者不冲突，且 `LOAD_DisableCompileOnLoad` 避免昂贵的 BP 编译。`DiffAssetsCommandlet` 里更朴素的做法是 `CopyFileToTempLocation` 到 `FPaths::DiffDir()` 后 `LoadPackage(..., LOAD_ForDiff)`（`DiffAssetsCommandlet.cpp:108`）。

### 2.3 蓝图图结构数据来源（基于 EdGraph 头文件与本仓库 IR dumper 实证）

| IR 字段 | UE 来源 | 是否稳定 | 备注 |
|---|---|---|---|
| `node_guid` | `UEdGraphNode::NodeGuid`（`FGuid`） | **较稳定**：节点创建后持久化；复制/粘贴、部分重建会变 | 首选匹配键 |
| `position` | `UEdGraphNode::NodePosX/NodePosY` | 稳定读取 | 仅参与**视觉 Diff**（`NODE_MOVED`/`MINOR`） |
| `size` | 评论框有 `UEdGraphNode_Comment::NodeWidth/NodeHeight`；普通节点尺寸由 UI 计算，序列化里无权威值 | 部分 | 普通节点尺寸建议不入逻辑 Diff |
| `title` | `UEdGraphNode::GetNodeTitle(ENodeTitleType::ListView)` | 稳定读取 | 参与匹配的第二层 |
| `comment` | `UEdGraphNode::NodeComment` | 稳定 | `NODE_COMMENT` |
| `flags.pure/latent/disabled` | `UK2Node::IsNodePure()`、函数 `HasMetaData(Latent)`、`EnabledState` | 可读 | 逻辑相关 |
| `pin_id` | `UEdGraphPin::PinId`（`FGuid`） | **一般稳定**，节点重建(`ReconstructNode`)后可能变 | 首选 Pin 匹配键 |
| `pin_name/direction/category/subcategory` | `UEdGraphPin::PinName`、`Direction`、`PinType.PinCategory/PinSubCategory/PinSubCategoryObject/ContainerType` | 稳定读取 | Pin 匹配次级键 |
| `default_value` | `UEdGraphPin::DefaultValue` / `DefaultObject` / `DefaultTextValue` | 稳定 | `PIN_DEFAULT_VALUE` |
| `linked_to`/`edges` | `UEdGraphPin::LinkedTo`（`TArray<UEdGraphPin*>`） | 稳定 | 连线 Diff |
| Comment Box | `UEdGraphNode_Comment`（含 bounds） | 稳定 | 单列一类节点 |
| Reroute | `UK2Node_Knot` | 稳定 | 视为透传节点或普通节点 |
| Macro/Function/Collapsed | `UBlueprint::MacroGraphs/FunctionGraphs`、`UK2Node_MacroInstance`、`UK2Node_Composite` | 稳定 | 每个 Graph 独立成一个 `graph` 条目 |

> **实证**：以上字段本仓库 `FBPGenIRDumper::DumpBlueprint`（`bpparser_testgen/Plugins/BPParserTestGen/Source/BPParserTestGen/Private/BPGenIRDumper.cpp`）已全部读取并序列化为 `bpat-ir-dump-1.0` JSON，并在 BP_01…BP_11 上跑通，可直接作为本方案 IR 抽取器的起点。

**逻辑 Diff vs 视觉 Diff 划分**：
- 逻辑：节点增删、`node_class` 变化、Pin 增删/类型/默认值、连线增删改。
- 视觉：`position`（NodeMoved）、评论框 bounds、纯排版。→ Review 中默认折叠"仅布局变化"。

### 2.4 UGit 侧 Diff 扩展点（基于解包 Electron 产物）

- **技术栈**：Electron（Chromium+Node）。主进程 `main.js`（144k 行，可读）、渲染层 `renderer.js`（React，452k 行，可读）、`other-renderer.js`，均带 `.map`。安装器为 Squirrel（`app-5.49.1`/`app-5.50.0` 双版本目录）。
- **已内置外部差异 Worker 先例**：`UExcelDiff.exe`。`installExcelDiff()`（`main.js:141747`）在安装/更新时把 `static/UExcelDiff.exe` 部署到根目录。→ 说明 UGit 本就有"**专用文件类型 → 外部 Worker**"的产品化路径。
- **可插拔 Diff 模型**：存在 `DiffType` 枚举，取值至少含 `Text / Excel / Image / SourcePreview`；diff 对象按变体携带不同 payload：
  - `DiffType.Text` → `leftBuffer/rightBuffer`（`renderer.js:218290`）
  - `DiffType.Excel` → `previous/current.contents`（`:218294`）
  - `DiffType.Image` → `previous/current.bytes`（`:218298`）
- **按扩展名分发**：`excelPreviewExts=['.xlsx']`（`renderer.js:173440/224193`）、`imageFileExtensions.has(extension)`（`:193273`）、`getExcelFileExtensions()`（`:212060`）。构造 diff 对象处（如 `:451203 kind: DiffType.Image`）即是**新增 `.uasset → DiffType.Blueprint` 分支的位置**。
- **已"懂" UE**：`static/gitlfs/UnrealEngine.gitattributes`、`static/gitignore/UnrealEngine.gitignore`、`.uasset` 归入 LFS 规则、`unreal-icon` 图标（`renderer.js:246615/249090`）。
- **已内置 JS 版 `.uasset` 解析（但仅包级）**：`app/src/utils/uasset-read/basic-parsers/uasset-read.js`（`renderer.js:484913+`）能解析 `.uasset` 头（`ExportCount/ExportOffset/ThumbnailTableOffset/NamesReferencedFromExportDataCount`、`EPackageFileTag`、UE4/UE5 版本号）、OFPA（One-File-Per-Actor）路径、`preCheckIsUAssetFile`、读取**内嵌缩略图**（`ThumbnailTableOffset`）。→ 这是**包/注册表级**能力，**不足以**还原 Blueprint 图结构（节点/Pin/连线），因此不能独立支撑路线 C 的图 Diff；但其"读缩略图"能力对路线 D 很有用。
- **内置 git**：`resources/app/git/...`（mingw64），可用 `git cat-file`/`git show :<sha>:<path>` 取 old/new blob。
- **取 old/new blob**：UGit 每条 diff 已能拿到左右两侧内容（Text 有 buffer、Image 有 bytes）。对 `.uasset` 只需把左右 blob 落成临时文件交给 Worker。

---

## 3. 可选技术路线对比

| 维度 | A: UE Worker+IR+UGit Viewer | B: UGit 直链 UE GraphEditor | C: 纯外部解析 .uasset | D: UE 原生截图 |
|---|---|---|---|---|
| 可行性 | 高（两侧基建齐备） | 极低 | 中（仅浅层） | 中高 |
| 结果原生程度 | 高（复用 UE 真解析/真 Diff） | 最高 | 低（易与引擎不一致） | 最高（像素级） |
| 实现难度 | 中 | 极高 | 高（要重写 .uasset+K2 语义） | 中 |
| 维护成本 | 中（跟随引擎版本升级 Worker） | 极高（强绑定版本/模块） | 极高（每个自定义节点都要适配） | 中 |
| 性能 | 首次需起 UE（秒级~十秒级），可缓存/常驻 | N/A | 快但不可靠 | 需起 UE + 渲染 |
| 风险 | 环境依赖、启动成本 | 授权/模块边界/体积/版本锁死 | 自定义节点/插件节点解析必然遗漏 | 无法定位到 Pin 级、diff 粒度粗 |
| 推荐度 | **★ 主线** | ✗ 不推荐 | △ 仅作降级/预览 | ○ 增强/参照 |

- **B 不推荐**：Slate/GraphEditor 是 Editor-only、体量巨大、与具体 UE 版本强绑定，链进 Electron 应用在体积、启动、跨版本、授权分发上都不可接受。
- **C 不推荐做主线**：`.uasset` 是版本化二进制 + 依赖 Name/Import/Export 表，Blueprint 的图结构（`UEdGraph`/`UK2Node`）依赖大量类型信息与自定义/插件节点；纯外部解析无法可靠还原，且每次引擎/插件升级都会破。UGit 现有 JS 解析只到包级，印证了"浅层可行、图级不可行"。可作为**无引擎环境时的降级**（只显示"这是蓝图 XXX、缩略图、包级摘要"）。
- **D 作为增强**：UE 侧用 `FWidgetRenderer`/GraphEditor 离屏渲染出 old/new 两张 PNG，UGit 叠加/并排；直观但无法点击定位到 Pin/连线，适合"总览参照"，与 A 的结构化 Diff 互补。

---

## 4. 推荐总体架构

```text
┌────────────────────────── UGit (Electron/React) ──────────────────────────┐
│  文件变更列表 (.uasset)                                                     │
│      │  按扩展名分发: 命中 .uasset → kind: DiffType.Blueprint               │
│      ▼                                                                      │
│  BlueprintDiffProvider (新增, 渲染进程/主进程)                              │
│      │ 1) git cat-file 取 old blob、取 working/new blob → 落临时文件         │
│      │ 2) 查缓存(按 blobSha+extractorVer+engineVer) 命中则跳过 Worker        │
│      │ 3) IPC 调 main 进程 spawn UE Worker                                   │
│      ▼                                                                      │
│  BlueprintDiffViewer (新增 React 组件, SVG/Canvas)                          │
│      - 三视图: 变更列表 / Overlay / 左右对比 ; 点击定位                       │
└───────────────▲───────────────────────────────────────────┬───────────────┘
                │ diff.json / old.ir.json / new.ir.json       │ spawn
                │                                             ▼
        ┌───────┴──────────────── UE Worker (UnrealEditor-Cmd -run=BPBlueprintDiff) ───────┐
        │  DiffUtils::LoadPackageForDiff(old 临时包)  +  Load(new /Game 或临时包)           │
        │      (LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks) │
        │  → 取两个 UBlueprint 的各 Graph                                                   │
        │  → FBPGenIRDumper 导出 old.ir.json / new.ir.json (供 Viewer 画布)                 │
        │  → FGraphDiffControl::DiffGraphs 每对 Graph → FDiffSingleResult[]                 │
        │  → 归一化为 diff.json (逻辑/视觉分类 + 置信度)                                    │
        │  → (可选 路线D) GraphEditor/WidgetRenderer 截图 old.png/new.png                    │
        └──────────────────────────────────────────────────────────────────────────────────┘
```

抽象命名建议（UGit 侧）：`DiffProvider`(通用) → `BlueprintDiffProvider`；`DiffType.Blueprint`；`BlueprintDiffViewer`；`UEWorkerClient`（封装 spawn/超时/降级/缓存）。

---

## 5. Blueprint IR 设计

沿用并小幅扩展本仓库已验证的 `bpat-ir-dump` 结构（新增 `stable_id`、`size`、`flags`、graph 层级）：

```json
{
  "asset": "/Game/Blueprints/BP_Player",
  "asset_type": "Blueprint",
  "engine_version": "5.4.4-...",
  "extractor_version": "bp-ir-2.0",
  "side": "old",
  "graphs": [{
    "graph_id": "EventGraph", "graph_name": "EventGraph", "graph_type": "UberGraph",
    "nodes": [{
      "stable_id": "EventGraph::K2Node_Event::ReceiveBeginPlay#<guid8>",
      "node_guid": "ABBC...", "node_class": "K2Node_Event", "title": "Event BeginPlay",
      "position": {"x":0,"y":0}, "size": {"w":0,"h":0}, "comment": "",
      "flags": {"pure": false, "latent": false, "disabled": false},
      "pins": [{
        "stable_id": "<node.stable>|out|then", "pin_id": "7ABF...", "pin_name": "then",
        "display_name": "", "direction": "output", "category": "exec", "subcategory": "",
        "default_value": "", "linked_to": ["<to_node.stable>|in|execute"]
      }]
    }],
    "edges": [{
      "stable_id": "<from.stable>->|<to.stable>", "from_node":"", "from_pin":"",
      "to_node":"", "to_pin":"", "edge_type":"exec"
    }]
  }]
}
```

字段确认（源码依据见 2.3）：
- `node_guid` 稳定但非绝对（重建/复制会变）→ 故引入组合式 `stable_id`。
- `pin_id`(`FGuid`) 一般稳定，`ReconstructNode` 后可能变 → Pin 也用组合式 `stable_id` 兜底。
- 类型/方向/默认值/连线/坐标/标题/注释来源均在 2.3 表中给出。
- **逻辑 Diff 字段**：node 存在性、node_class、pins（名/类型/默认值/连线）；**视觉 Diff 字段**：position、size、comment bounds。

---

## 6. Diff 模型设计

直接映射 UE `EDiffType`（`DiffResults.h`）+ 补齐图/注释级：

| 类别 | 我方 Diff 类型 | UE 对应/来源 | 逻辑/视觉 | Review 高亮 |
|---|---|---|---|---|
| 节点 | NodeAdded/Removed | `NODE_ADDED/REMOVED` | 逻辑 | 高 |
| 节点 | NodeModified | 汇总下列 | 逻辑 | 高 |
| 节点 | NodeMoved | `NODE_MOVED`(MINOR) | 视觉 | 默认折叠 |
| 节点 | NodeClassChanged | 匹配后 class 不同 | 逻辑 | 高 |
| 节点 | CommentChanged | `NODE_COMMENT` | 视觉/半逻辑 | 中 |
| Pin | PinAdded/Removed | `NODE_PIN_COUNT` + 明细 | 逻辑 | 高 |
| Pin | PinTypeChanged | `PIN_TYPE_CATEGORY/SUBCATEGORY/IS_ARRAY/IS_REF` | 逻辑 | 高 |
| Pin | PinDefaultValueChanged | `PIN_DEFAULT_VALUE` | 逻辑 | 高 |
| 连线 | EdgeAdded/Removed | `PIN_LINKEDTO_NUM_INC/DEC` | 逻辑 | 高 |
| 连线 | EdgeRewired | `PIN_LINKEDTO_NODE/PIN` | 逻辑 | 高 |
| 图 | GraphAdded/Removed/Renamed | 我方按 Graph 集合比较 | 逻辑 | 高 |
| 布局 | CommentBoxAdded/Removed、LayoutOnlyChanged | 我方 | 视觉 | 默认折叠 |

- **"移动 vs 删除+新增" 的判定**：先做节点匹配（见 §7）；匹配成功但仅 position 变 → `NodeMoved`；匹配失败才算增/删。
- **GUID 变了但语义近似**：靠匹配第 2~4 层（class+title+graph、pin 签名+邻近、启发式相似度）识别为"同一节点被修改"，并在 `diff.json` 标 `match_confidence`。
- `diff.json` 每条含：`type`、`change_class`(logical/visual)、`severity`(major/minor/info)、`graph_id`、`old_ref/new_ref`(stable_id)、`fields`(变化明细)、`match_confidence`。

---

## 7. 节点 / Pin / 连线匹配策略

UE 已提供 `FGraphDiffControl::FindNodeMatch`/`IsNodeMatch`（`GraphDiffControl.h:112/121`）——**优先复用**，我方仅在其之上补"跨版本 stable_id + 置信度标注"。

```text
节点匹配：
  L1 NodeGuid 精确              → confidence=1.0
  L2 node_class + title + graph_path → 0.9
  L3 node_class + pin_signature + 邻近 position → 0.7
  L4 启发式相似度(pins/连线重合度)  → 0.4~0.6 (标记需人工确认)
Pin 匹配：
  L1 PinId 精确 → L2 pin_name+direction+type → L3 display_name+direction+category
连线匹配：
  from_node.stable_id + from_pin.stable_id + to_node.stable_id + to_pin.stable_id
```

- **误判场景**：批量删后重建（GUID 全变）、复制粘贴子图、节点被替换为同类不同函数、宏展开差异。→ 用 L2/L3 兜底并标低置信度；置信度低于阈值的"修改"降级提示为"疑似 增/删"。
- **与 UE 逻辑结合**：图级 Diff 用 `DiffGraphs` 的结果作"权威逻辑差异"；我方匹配层主要服务于**Viewer 的视觉对齐与定位**（把 old/new 节点在两张画布/一张 Overlay 上对上）。

---

## 8. UGit Viewer 设计

技术栈结论：UGit 是 Electron+React，**天然适合 WebView + SVG/Canvas**，无需原生图形控件。复用其 `DiffType` 分发即可挂载新组件。

三视图与优先级：
1. **Diff 列表 + 点击定位（P0，先做）**：左侧变更列表（按 graph 分组、逻辑/视觉分类、可折叠"仅布局"），右侧 SVG 画布；点击某条 → 高亮并居中对应节点/Pin/连线。
2. **单图 Overlay（P0）**：old∪new 合并到一张图，配色：绿=新增、红=删除、黄=修改、灰=未变、蓝虚线=仅移动。
3. **左右对比（P1）**：左旧右新，同步缩放/平移。

细节：
- 颜色/严重度来自 `diff.json`；Pin 默认值变化在节点上以 `旧→新` 徽标显示；连线变更用高亮边 + 端点标记。
- 大型蓝图：SVG 虚拟化/视口裁剪 + 缩略图导航 + "只看变更节点"过滤 + 搜索定位。
- 不追求 UE 原生皮肤；用简化节点卡（标题条 + Pin 列表），保证可读与 Review 效率。
- 画布数据来自 `old.ir.json/new.ir.json`（几何+内容），叠加 `diff.json`（着色/定位）。

---

## 9. PoC 实施方案

**目标**：对一个 UE 工程内单个 Blueprint 的 EventGraph，在 UGit 中显示结构化 Diff（节点增删、连线增删、Pin 默认值变化、节点移动），支持"变更列表 + 单图 Overlay + 点击定位"。

**输入/输出**：
- 输入：`old.uasset`(git blob 落临时文件) + `new.uasset`(工作区) + UERoot + ProjectUProject。
- 输出：`old.ir.json`、`new.ir.json`、`diff.json`（+ 可选 `old.png/new.png`）。

**UE 侧改动（复用本仓库插件，几乎零新增基建）**：
- 新增 commandlet `-run=BPBlueprintDiff`（放进现有 `BPParserTestGen` 插件）：
  - 用 `DiffUtils::LoadPackageForDiff` 载 old（临时包）、载 new；
  - 复用 `FBPGenIRDumper` 出两份 IR；
  - 调 `FGraphDiffControl::DiffGraphs` 出 `FDiffSingleResult[]`，序列化为 `diff.json`；
  - Build.cs 增 `GraphEditor` 依赖（`FGraphDiffControl`）。
- 参数：`-OldFile= -NewFile= -OldAssetPath= -OutputDir=`。

**UGit 侧改动（源码工程内）**：
- `binary`/扩展名表新增 `.uasset` → 在 diff 构造分发点（`renderer.js` 同 `imageFileExtensions.has` 处）产出 `kind: DiffType.Blueprint`；
- 新增 `BlueprintDiffProvider`（取 blob→临时文件→缓存→IPC spawn Worker→读 JSON）；
- 新增 `BlueprintDiffViewer` React 组件（SVG 画布 + 列表 + 定位）；
- 主进程新增 IPC handler：spawn `UnrealEditor-Cmd -run=BPBlueprintDiff ...`，超时/失败降级。

**命令行示例**：
```powershell
UnrealEditor-Cmd.exe "E:\BPTestProject\BPTest\BPTest.uproject" -run=BPBlueprintDiff `
  -OldFile="%TEMP%\bpdiff\BP_X__old.uasset" -NewFile="E:\...\Content\...\BP_X.uasset" `
  -OldAssetPath="/Game/.../BP_X" -OutputDir="%TEMP%\bpdiff\out" -unattended -nop4 -stdout
```

**JSON 输出示例（diff.json，节选）**：
```json
{ "schema_version":"1.0","asset":"/Game/.../BP_X","graph":"EventGraph",
  "changes":[
    {"type":"NodeAdded","change_class":"logical","severity":"major","new_ref":"EventGraph::K2Node_CallFunction::PrintString#a1b2","match_confidence":1.0},
    {"type":"PinDefaultValueChanged","change_class":"logical","severity":"major","old_ref":"...|in|B","fields":{"old":"8","new":"99"}},
    {"type":"NodeMoved","change_class":"visual","severity":"minor","old_ref":"...","fields":{"old":{"x":0,"y":0},"new":{"x":120,"y":-80}}}
  ]}
```

**Viewer 草图**：左列变更树（EventGraph ▸ +PrintString / ~Add.B / ⇄move Set），右侧 Overlay 画布（绿=新增节点、黄=默认值改、蓝虚线=移动）。

**预计工作量**（1 人）：UE commandlet 复用现有基建 ≈ 2~3 天；UGit Provider+IPC+缓存 ≈ 3~4 天；SVG Viewer（列表+Overlay+定位）≈ 5~7 天。**两周可出可演示版本。**

**主要风险**：需要一个能加载该资产的 UE 工程/引擎环境（见 §11）。

**如何验证 PoC 成功**：构造一次真实提交（增 1 节点 / 改 1 默认值 / 删 1 连线 / 移 1 节点），在 UGit 打开该 `.uasset` 的 diff，四类变化都被正确列出、着色、点击可定位；与 UE 编辑器内蓝图 Diff 工具结论一致。

---

## 10. 缓存与性能方案

- **是否每次都起 UE**：不必。按内容缓存 IR/diff；仅缓存未命中才起 Worker。
- **启动成本**：`UnrealEditor-Cmd` 冷启动数秒~十几秒；可用**常驻 Worker**（一个长期存活的 editor 进程 + 轻量 IPC/文件队列）把每次 diff 降到亚秒级；PoC 阶段可先"按需起、结果缓存"。
- **缓存 key**：`repo_id | engine_version | project_path | project_plugin_hash | asset_path | git_blob_sha | extractor_version`。任一变化即失效。
- **IR 按 blob 缓存**：`ir[blob_sha][extractor_version]`；diff 缓存 `diff[old_sha|new_sha|extractor_version]`。
- **批处理**：一次多文件 diff 时，把多资产喂给同一 Worker 会话，摊薄启动成本。
- **降级**：Worker 崩溃/超时/资产依赖缺失 → 回退到 UGit 现有 JS 包级解析（显示资产名、缩略图、包级摘要 + "结构 Diff 不可用"提示），绝不阻塞普通 diff。

---

## 11. 风险清单与规避方案

| # | 风险 | 触发场景 | 影响 | 规避 | 阻塞 PoC |
|---|---|---|---|---|---|
| 1 | UE 版本兼容 | Worker 与工程引擎版本不一致 | 加载失败/字段错位 | Worker 跟工程 `EngineAssociation` 走；`LOAD_DisableEngineVersionChecks`；缓存含 engine_version | 否 |
| 2 | 自定义/插件节点 | 项目自定义 K2Node | 需插件已加载才能解析 | Worker 在**目标工程**上下文运行（插件自然加载）；这正是不选路线 C 的原因 | 否 |
| 3 | old/new 同名包冲突 | 同一路径两版本 | 二次加载覆盖 | `DiffUtils::LoadPackageForDiff` 临时包 + `LOAD_ForDiff` | **否（已有解）** |
| 4 | 依赖资产缺失 | old blob 引用了已删资产 | 加载告警/部分失败 | `LOAD_ForDiff` 容忍；缺失依赖降级为 info | 否 |
| 5 | 大工程启动成本 | 每次 diff 起 UE | 体验差 | 缓存 + 常驻 Worker | 否 |
| 6 | UGit UI 集成复杂度 | 改打包产物难维护 | PoC 受阻 | 用 UGit **源码工程**实现；无源码则先 patch 演示 | **可能（取决于能否拿到 UGit 源码）** |
| 7 | Editor-only 模块授权/分发 | 把 UE 模块塞进 UGit | 授权/体积问题 | 只**外部调用** UnrealEditor-Cmd，不链接 UE 模块（避开路线 B） | 否 |
| 8 | Diff 匹配误判 | GUID 全变/复制子图 | 增删/移动混淆 | 多层匹配 + 置信度标注 + 低置信降级提示 | 否 |
| 9 | 移动 vs 逻辑变化混淆 | 大改排版 | 噪声淹没逻辑 diff | 逻辑/视觉分类 + 默认折叠"仅布局" | 否 |
| 10 | 二进制损坏/版本不匹配 | LFS 指针未拉取、blob 损坏 | 无法加载 | 先校验是否 LFS 指针；缺失时提示"未拉取 LFS" | 否 |
| 11 | 跨平台差异 | mac/linux | 路径/进程差异 | Worker 调用与临时目录抽象；PoC 先 Windows | 否 |

---

## 12. 下一步建议（最建议马上做的 3~5 件）

1. **确认 UGit 源码工程可得性**（阻塞项 6）：能拿到 UGit 源码则 PoC 走源码；否则先在解包产物上做一次性 patch 做技术演示。**这点需要与你确认。**
2. **在本仓库 `BPParserTestGen` 插件加 `-run=BPBlueprintDiff` commandlet**：复用 `LoadPackageForDiff` + `FBPGenIRDumper` + `FGraphDiffControl::DiffGraphs`，先产出 `old.ir.json/new.ir.json/diff.json`（不碰 UGit，先把 UE 侧闭环跑通、可 CLI 验证）。
3. **定义并冻结 IR/diff JSON schema v1** + `stable_id` 规则，作为 UE↔UGit 契约。
4. **做一个独立 HTML/SVG 原型 Viewer**（脱离 UGit 先验证可视化），吃 `diff.json` 显示"列表 + Overlay + 定位"。
5. **再把 2/4 接入 UGit 的 `DiffType.Blueprint`**，完成端到端 PoC，并按 §9 验证标准比对 UE 编辑器内蓝图 Diff。

---

## 13. 明确判断（对照 prompt 十三）

```text
1. 能不能做：能
2. 最推荐主线：A（UE Worker + IR/Diff + UGit WebView Viewer）
3. 不推荐主线：B（直链 UE GraphEditor）、C（纯外部解析做图 Diff）
4. PoC 第一阶段：UE 侧 -run=BPBlueprintDiff 产出 old/new IR + diff.json（CLI 可验证）
5. 可复用 UE 能力：DiffUtils::LoadPackageForDiff、FGraphDiffControl::DiffGraphs/FindNodeMatch、FDiffSingleResult/EDiffType、本仓库 FBPGenIRDumper
6. 必须自研：stable_id 与置信度、IR/diff schema 与序列化、UGit DiffType.Blueprint 视图与 SVG 画布、blob→临时文件→Worker 编排与缓存
7. UGit 接入点：DiffType 枚举 + 按扩展名构造 diff 对象的分发处(renderer.js ~L193273 同类分支) + 复用 UExcelDiff 式外部 Worker 部署
8. 最大风险：运行环境依赖（需可加载资产的 UE 工程/引擎）；其次是能否拿到 UGit 源码工程
9. 两周可演示需砍：左右对比视图、自动合并、常驻 Worker、路线D截图、非 EventGraph 图、跨平台——只留"EventGraph + 列表 + Overlay + 4 类变更 + 按需起 Worker+缓存"
10. 产品化补齐：常驻 Worker 池、全图类型(Function/Macro/Anim 等)支持、更强匹配与置信度、增量/批处理、跨平台、LFS 未拉取处理、UI 皮肤与大图性能优化

推荐主线：A
不推荐主线：B、C
PoC 优先级：高
技术阻塞：无（工程/源码可得性为环境前提，非技术阻塞）
最大风险：运行环境依赖 + UGit 源码可得性
最小可演示版本：单 Blueprint EventGraph 的结构化 Diff（节点/连线增删 + Pin 默认值 + 移动），UGit 内"变更列表 + 单图 Overlay + 点击定位"，UE 侧按需起 Worker 并按 blob 缓存
```
