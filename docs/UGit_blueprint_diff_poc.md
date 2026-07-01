# Blueprint 结构 Diff PoC（P1 + P2，已验证）

对应探索报告 `docs/UGit_blueprint_diff_exploration_report.md` 的插件化三层架构中的 **L3 UE Worker** 与 **L2 独立 Viewer**。二者均**宿主无关**、不依赖 UGit 源码，可独立运行/验证。UGit 侧的 L1 可开关适配器留待拿到 UGit 源码后接入。

## 组件

| 层 | 组件 | 位置 |
|---|---|---|
| L3 UE Worker | `-run=BPBlueprintDiff` commandlet + `FBPBlueprintDiff` | `bpparser_testgen/Plugins/BPParserTestGen/Source/BPParserTestGen/{Public,Private}/BPBlueprintDiff*.{h,cpp}` |
| L2 独立 Viewer | 单文件 HTML/SVG（无依赖） | `bpparser_testgen/deliverables/bp_diff_viewer/index.html`（+ `sample/` 演示数据） |

L3 复用：`DiffUtils::LoadPackageForDiff`（版本隔离加载）、`FGraphDiffControl::DiffGraphs`（UE 原生图 Diff）、本仓库 `FBPGenIRDumper`（几何 IR）。

## 运行方式

**Worker（产出 old.ir.json / new.ir.json / diff.json / manifest.json）**

模式 A（两个不同资产，PoC 验证用）：
```powershell
UnrealEditor-Cmd.exe "<Project>.uproject" -run=BPBlueprintDiff `
  -OldAssetPath="/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -NewAssetPath="/Game/BPParserScratch/BP04_variant" `
  -OutputDir="<out>" -unattended -nopause -nop4 -stdout
```

模式 B（同名资产的两个版本 / git blob，生产路径）：
```powershell
... -run=BPBlueprintDiff -OldFile="<old.uasset 临时文件>" -OldAssetPath="/Game/.../BP" [-NewFile="<new.uasset>" | -NewAssetPath="/Game/.../BP"] -OutputDir="<out>"
```
模式 B 用 `DiffUtils::LoadPackageForDiff` 将 old 版本以临时包隔离加载（`LOAD_ForDiff | LOAD_DisableCompileOnLoad`），规避同名包冲突。

退出码：`0 成功 / 10 部分 / 20 失败 / 30 参数错误`。

**Viewer**

```powershell
cd bpparser_testgen/deliverables/bp_diff_viewer
python -m http.server 8777       # 然后浏览器打开 http://127.0.0.1:8777/index.html（自动加载 ./sample）
```
也可直接用 `file://` 打开，用页面上的文件选择器手动加载 `old.ir.json / new.ir.json / diff.json`（或整目录）。

## diff.json Schema（bp-diff-1.0）

```json
{
  "schema_version": "bp-diff-1.0",
  "engine_version": "5.4.4-...",
  "asset_old": "/Game/.../BP.BP",
  "asset_new": "/Game/.../BP.BP",
  "summary": { "total": 2, "logical": 2, "visual": 0, "by_type": { "PinDefaultValueChanged":1, "NodeAdded":1 } },
  "graphs": [
    { "graph": "EventGraph", "graph_type": "ubergraph",
      "changes": [
        { "type":"PinDefaultValueChanged", "raw_diff_type":"PIN_DEFAULT_VALUE",
          "category":"modification", "change_class":"logical", "severity":"major",
          "node1_guid":"...", "node1_title":"Delay", "node2_guid":"...", "node2_title":"Delay",
          "pin1":"Duration", "pin2":"Duration", "message":"Pin Default 'Duration' '0.2' -> '0.5'" },
        { "type":"NodeAdded", "raw_diff_type":"NODE_ADDED", "category":"addition",
          "change_class":"logical", "severity":"major", "node1_guid":"...", "node1_title":"Print String",
          "message":"Added Node 'Print String'" }
      ] }
  ]
}
```

- `type`：稳定语义类型（NodeAdded/NodeRemoved/NodeMoved/CommentChanged/PinCountChanged/PinDefaultValueChanged/PinTypeChanged/EdgeAdded/EdgeRemoved/EdgeRewired/NodeModified/GraphAdded/GraphRemoved）。
- `raw_diff_type`：UE `EDiffType` 原始枚举名（可追溯）。
- `change_class`：`logical`（进 Review 高亮）/ `visual`（默认可折叠：NodeMoved、CommentChanged）。
- `severity`：`major/minor/info`（源自 UE `EDiffType::Category`）。
- 节点引用用 `NodeGuid`；Pin 引用用 pin 名（pin 级 diff 会回填其 owning node guid，便于定位）。
- `old.ir.json/new.ir.json`：沿用 `bpat-ir-dump`（含节点坐标/pins/edges/comments），供 Viewer 画布几何。

## 已验证结果（BP_04 原版 vs 变体）

变体 = 在 BP_04 副本上改 `Delay.Duration 0.2→0.5` + 新增一个 `Print String`。Worker 输出 `total=2 logical=2 visual=0`；`PinDefaultValueChanged(Delay.Duration)` 的信息串 `'0.2' -> '0.5'` 直接来自 UE 原生 Diff。Viewer 正确渲染整图并着色：**新增节点=绿、修改节点=黄**，左侧变更列表可点击定位。截图见提交记录。

## Viewer 三视图进度

- ✅ 单图 Overlay（着色：绿=新增 / 红=删除 / 黄=修改 / 蓝虚线=移动 / 灰=未变）
- ✅ 变更列表 + 点击定位 + 逻辑/视觉过滤
- ⏳ 左右对比视图（P1 之后再补）

## 下一步

1. 冻结 `bp-diff-1.0` / `bpat-ir-dump` 为正式契约。
2. 增强连线级可视化（按 pin 锚点画边、EdgeRewired 高亮）与 `stable_id`（跨 GUID 变更的匹配置信度）。
3. 模式 B（git blob→临时包）端到端验证。
4. （需 UGit 源码）L1 可开关旁路适配器：EnableGate + `DiffType.Blueprint` 内嵌本 Viewer。
