// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Read-only guard. Subscribes to package dirty / save events during dump and
 * aborts on any modification of protected packages (/Game/, /Engine/, /Script/, /Config/).
 *
 * See docs/readonly_safety.md L2, L3.
 */
class FBPATReadOnlyGuard
{
public:
	void Begin();
	void End();

	bool HasViolation() const { return bViolated; }
	int32 GetDirtyPackageCount() const { return DirtyPackages.Num(); }

private:
	bool bActive = false;
	bool bViolated = false;
	TArray<TWeakObjectPtr<UPackage>> DirtyPackages;

	FDelegateHandle DirtyHandle;
	FDelegateHandle SavedHandle;
};
