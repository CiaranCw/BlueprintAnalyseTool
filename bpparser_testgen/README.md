# BPParserTest 生成器 — 使用说明

为「Unreal Engine 蓝图解析 Agent」构建的一套系统化测试资产生成器（面向 **UE 5.4**）。
本目录有两部分：

```
bpparser_testgen/
├─ Plugins/BPParserTestGen/        UE 5.4 C++ Editor 插件（真正生成 /Game/BPParserTest 资产）
└─ deliverables/                   预期解析 JSON / 可视化 / 覆盖矩阵 / 报告（静态交付物）
   ├─ expected_ir/   *.json        每个资产的“预期解析”基线（与 BPAT IR 对齐）
   ├─ viz/           *.dot *.mmd   每个测试蓝图的结构图源码
   ├─ render_viz.ps1               把 dot/mmd 渲染成 PNG/SVG（本地运行）
   ├─ coverage_matrix.md           覆盖矩阵
   ├─ report.md                    总报告（资产清单/编译/限制/检查指南汇总）
   ├─ manual_check_guide.md        人工检查清单
   └─ regression_protocol.md       修改后回归解析协议
```

> **生成方式 = C++ Editor 插件 + Commandlet/编辑器菜单。** 不伪造 `.uasset`；资产由你在 UE 中执行后真实生成。
> 选 C++ 而非纯 Python：UE 5.4 的 `unreal` Python API **无法可靠创建 K2 图节点和连线**，C++ 才能完整覆盖节点/Pin/连线。

---

## 1. 安装插件

把 `Plugins/BPParserTestGen` 整个目录复制到目标工程的 `Plugins/` 下：

```
E:\BPTestProject\BPTest\Plugins\BPParserTestGen\
```

PowerShell：

```powershell
$src = "E:\BlueprintAnalyseTool\bpparser_testgen\Plugins\BPParserTestGen"
$dst = "E:\BPTestProject\BPTest\Plugins\BPParserTestGen"
robocopy $src $dst /E
```

> `BPTest` 当前是纯蓝图工程。加入带 C++ Source 的插件后，需要一次 C++ 编译（见下）。

## 2. 生成工程文件并编译

1. 右键 `E:\BPTestProject\BPTest\BPTest.uproject` → **Generate Visual Studio project files**
   （或命令行 `UnrealBuildTool -projectfiles`）。
2. 用 VS 打开 `BPTest.sln`，配置 **Development Editor / Win64**，**Build**。
   - 若无 C++ 工具链：安装 Visual Studio 2022 + “使用 C++ 的游戏开发”工作负载。
3. 编译成功后启动编辑器；`BPParserTestGen` 插件应已加载（Editor 类型，默认启用本工程）。

## 3. 运行生成（两种方式，任选其一）

**A. 编辑器内（推荐先用这种）**
- 顶部菜单 **Tools → BP Parser Test → Generate BP Parser Test Suite**，或
- 控制台（`~`）输入：`BPParserTest.Generate`
- 完成后弹窗显示 attempted / ok / warnings / failed 统计。

**B. 无界面 Commandlet（CI 友好）**

```powershell
& "<UE_5.4>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "E:\BPTestProject\BPTest\BPTest.uproject" `
  -run=BPParserTestGen
```

## 4. 产物位置

- 资产：Content Browser 的 **`/Game/BPParserTest/`**（16 个：5 支持资产 + BP_01..BP_10 + BP_99）。
- 生成报告：**`E:\BPTestProject\BPTest\Saved\BPParserTestReports\generation_log.json`**
  （含每个资产 created / saved / **compile_status** / notes —— 这是“是否编译通过”的权威来源）。

## 5. 出图（PNG/SVG）

本生成器**不产图片**。本地渲染：

```powershell
cd E:\BlueprintAnalyseTool\bpparser_testgen\deliverables
powershell -ExecutionPolicy Bypass -File .\render_viz.ps1
# 需要 Graphviz(dot) 和/或 Mermaid CLI(mmdc)；缺失则只跳过对应格式
```

## 6. 重新生成 / 清理

生成器对已存在资产会先重命名到 transient 再创建（支持重跑）。若想彻底清空，删除 Content Browser 中的 `/Game/BPParserTest/` 后重跑即可。

## 7. 重要前提

- 全部资产是否**编译通过**、PNG 是否生成，必须由你在 UE 5.4 实跑确认 —— 本工具的作者环境无法编译 UE。
- `deliverables/` 下的 JSON/DOT/MMD 是**预期基线**；以 BPAT 解析器对真实 `.uasset` 的输出为准做回归对比。
- 详见 `report.md`「已知限制」与 `coverage_matrix.md`。
