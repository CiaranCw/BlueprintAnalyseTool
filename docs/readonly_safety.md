# 只读安全方案

> **最高优先级约束。** 整个正向解析阶段必须只读：除了显式输出目录 `OutputDir`，禁止任何写入。

---

## 1. 七层防护

| 层 | 名称                       | 实现位置                              |
| -- | -------------------------- | ------------------------------------- |
| L1 | 路径白名单                 | `BPATPathPolicy` / `BPATOutputDirManager` |
| L2 | SavePackage 钩子           | `BPATReadOnlyGuard`                   |
| L3 | Dirty 检测                 | `BPATReadOnlyGuard`                   |
| L4 | 编译禁用                   | `BPATEditor.Build.cs`（依赖白名单）   |
| L5 | CDO 不可写                 | reader 编码约束                       |
| L6 | Overwrite 控制             | `BPATOutputDirManager`                |
| L7 | Dry-run 模式               | `BPATIRSerializer`                    |
| L8（可选）| 进程外指纹回归       | `scripts/verify_readonly.py`          |

> L8 标号 8 但保留单独章节，逻辑上属于"七层 + 一层 CI 回归"。

---

## 2. L1 路径白名单

`BPATPathPolicy::AssertWritable(Path)` 在每次写文件前调用。规则：

1. 路径必须是绝对路径。
2. 路径必须在 `OutputDir` 之下。
3. `OutputDir` 必须满足：
   - 不在 `<ProjectPath>` 下（含 `Content/`、`Source/`、`Config/`、`Saved/`、`Intermediate/`、`Plugins/`）；
   - 不在 `<EnginePath>` 下；
   - 不在系统根目录。
4. `BPATIRSerializer` 与所有 Exporter 必须**只通过** `BPATPathPolicy::OpenWriteFile` 写盘。

实现要点（伪代码）：

```cpp
void BPATPathPolicy::AssertWritable(const FString& AbsPath, const FString& OutputDirAbs)
{
    checkf(FPaths::IsRelative(AbsPath) == false, TEXT("Path must be absolute"));
    const FString Norm = FPaths::ConvertRelativePathToFull(AbsPath);
    const FString OutNorm = FPaths::ConvertRelativePathToFull(OutputDirAbs);
    if (!Norm.StartsWith(OutNorm))
    {
        UE_LOG(LogBPAT, Fatal, TEXT("E2002 OUTPUT_DIR_INSIDE_PROJECT or write outside OutputDir: %s"), *Norm);
    }
}
```

---

## 3. L2 SavePackage 钩子

```cpp
ReadOnlyGuard.SaveHandle =
    UPackage::PackageSavedWithContextEvent.AddLambda(
        [](const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext)
        {
            if (Package && IsProtectedPackagePath(Package->GetName()))
            {
                UE_LOG(LogBPAT, Fatal,
                       TEXT("E2001 READ_ONLY_VIOLATION: package %s saved during dump"),
                       *Package->GetName());
            }
        });
```

`IsProtectedPackagePath`：以 `/Game/`、`/Engine/`、`/Script/`、`/Config/` 为前缀的包路径都受保护。

> 钩子名称【待验证 V1】：UE 5.4 实际 delegate 名以源码为准；可能是 `UPackage::PackageSavedWithContextEvent` 或 `FCoreUObjectDelegates::OnPackageSavedWithContext`。

---

## 4. L3 Dirty 检测

```cpp
ReadOnlyGuard.DirtyHandle =
    UPackage::PackageMarkedDirtyEvent.AddLambda(
        [](UPackage* Pkg, bool bWasDirty)
        {
            if (Pkg && IsProtectedPackagePath(Pkg->GetName()))
            {
                ReadOnlyGuard.RecordViolation(Pkg);
            }
        });
```

流程结束时：

```cpp
TArray<UPackage*> Dirty;
for (UPackage* P : LoadedPackages)
    if (P->IsDirty()) Dirty.Add(P);
if (Dirty.Num() > 0)
    return ExitCode_ReadOnlyViolation;  // 40
```

---

## 5. L4 编译禁用

`BlueprintAgentToolsEditor.Build.cs` **不依赖**：

- `KismetCompiler`
- `BlueprintCompilationManager` 相关
- `UnrealEd`（除非确实必要；尽量避开 `FBlueprintEditorUtils` 的写函数）

代码评审 checklist：

- 不准 `#include "KismetCompiler*"`
- 不准 `Compile*Blueprint`
- 不准 `MarkBlueprintAsModified`、`MarkBlueprintAsStructurallyModified`
- 不准 `FAssetEditorManager::OpenEditorForAsset`

---

## 6. L5 CDO 不可写

读取 CDO 默认值时只能：

```cpp
UClass* GenClass = BP->GeneratedClass;
const UObject* CDO = GenClass->GetDefaultObject(false /* bCreateIfNeeded */);
// 仅做 const 访问；不调用 CDO->Modify()
```

任何调用 `GetDefaultObject(true)` 的写法在评审时拒绝合并。

---

## 7. L6 Overwrite 控制

```text
--OverwritePolicy=skip       默认。已有同名输出 → 跳过该资产
--OverwritePolicy=overwrite  先把旧目录改名 <dir>.bak.<timestamp>，再写新
--OverwritePolicy=fail       已有任意冲突 → 退出码 30
```

> `overwrite` 模式不直接覆盖：先备份，再写。失败时回滚。

---

## 8. L7 Dry-run

`--DryRun=1`：

- reader / IRBuilder 全部正常运行
- IRValidator 正常运行
- IRSerializer **改用** `FBPATDryRunSink`：累计应写文件路径、字节数、内容哈希
- 流程结束输出 `<OutputDir>/dry_run_report.json` 到指定 dry-run 目录（仍受 L1 约束）

---

## 9. L8 进程外指纹回归（CI）

`scripts/verify_readonly.py`：

```text
1. snapshot_project(ProjectPath) → before.json
2. run_dump(...)
3. snapshot_project(ProjectPath) → after.json
4. diff(before, after)
   - 期望为空：路径增加 / 删除 / 大小变化 / mtime 变化都视为失败
```

为减少 false positive：可选忽略 `Saved/` 目录（UE 自身可能在那里写日志）；但**不能**忽略 `Content/`、`Source/`、`Config/`。

---

## 10. 禁用行为黑名单（实现层）

实现代码中**永远不准出现**：

- `UPackage::SavePackage`、`SavePackageHelper`
- `UBlueprint::Modify()`、`SetFlags(RF_Transactional | RF_*)` 然后改属性
- `FBlueprintEditorUtils::MarkBlueprintAsModified`
- `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified`
- `FKismetEditorUtilities::CompileBlueprint`
- `FAssetEditorManager::OpenEditorForAsset`
- `IFileManager::Get().Delete(<不在 OutputDir 下的路径>)`
- 任何对 `<ProjectPath>/Content` 的写操作

CI 在静态扫描阶段对源码做 grep；命中即失败。

---

## 11. 默认参数

| 参数              | 默认值       |
| ----------------- | ------------ |
| `--StrictReadOnly`| `1`          |
| `--OverwritePolicy`| `skip`      |
| `--DryRun`        | `0`          |

`--StrictReadOnly=0` 仅供反向阶段使用；当前阶段在代码里**禁止**走 `0` 分支（直接拒绝并 exit 30）。
