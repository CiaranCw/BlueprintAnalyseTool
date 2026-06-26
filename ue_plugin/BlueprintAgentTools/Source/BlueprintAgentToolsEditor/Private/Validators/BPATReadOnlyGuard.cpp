// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Validators/BPATReadOnlyGuard.h"
#include "Util/BPATPathPolicy.h"
#include "BPATLog.h"
#include "BPATErrorCodes.h"
#include "UObject/Package.h"

void FBPATReadOnlyGuard::Begin()
{
	check(!bActive);
	bActive = true;
	bViolated = false;
	DirtyPackages.Reset();

	// V1 (待验证): UE 5.4 中 SavePackage 钩子精确名。
	// 当前实现使用 UPackage::PackageSavedWithContextEvent；
	// 若该 delegate 在目标版本不存在，需切换到
	// FCoreUObjectDelegates::OnPackageSavedWithContext。
	DirtyHandle = UPackage::PackageMarkedDirtyEvent.AddLambda(
		[this](UPackage* Pkg, bool /*bWasDirty*/)
		{
			if (Pkg && FBPATPathPolicy::IsProtectedPackagePath(Pkg->GetName()))
			{
				bViolated = true;
				DirtyPackages.AddUnique(Pkg);
				UE_LOG(LogBPAT, Error,
					TEXT("%s: package marked dirty during dump: %s"),
					BPATErrorCodes::ReadOnlyViolation, *Pkg->GetName());
			}
		});

	// SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(...)
	// 等到 V1 验证完成后启用。
}

void FBPATReadOnlyGuard::End()
{
	if (!bActive) return;

	UPackage::PackageMarkedDirtyEvent.Remove(DirtyHandle);
	DirtyHandle.Reset();

	if (DirtyPackages.Num() > 0)
	{
		bViolated = true;
		UE_LOG(LogBPAT, Error,
			TEXT("%s: %d protected package(s) became dirty during dump."),
			BPATErrorCodes::ReadOnlyViolation, DirtyPackages.Num());
	}

	bActive = false;
}
