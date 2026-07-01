// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPBlueprintDiffCommandlet.generated.h"

/**
 * Headless structural diff between two Blueprint assets (L3 worker of the plugin architecture).
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=BPBlueprintDiff \
 *       -OldAssetPath=/Game/.../BP_Old -NewAssetPath=/Game/.../BP_New \
 *       -OutputDir="<dir>"  -unattended -nop4 -stdout
 *
 *   (revision mode) ... -OldFile="<tempfile>" [-NewFile="<file>" | -NewAssetPath=/Game/...] -OldAssetPath=/Game/...
 *
 * Outputs old.ir.json / new.ir.json / diff.json / manifest.json.
 * Exit codes: 0 success, 10 partial, 20 failed, 30 bad input.
 */
UCLASS()
class UBPBlueprintDiffCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
