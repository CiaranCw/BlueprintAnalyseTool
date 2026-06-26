// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPATEditCommandlet.generated.h"

/**
 * Applies a structured atomic edit request to a Blueprint and writes machine-readable artifacts.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=BPATEdit \
 *       -AssetPath=/Game/BPParserTest/BP_04_ExecFlow_Control \
 *       -EditRequestJson="<file>.json" \
 *       -OutputDir="<dir>" \
 *       -Mode=apply-and-verify  (plan-only|dry-run|apply|apply-and-verify) \
 *       -CreateBackup=1 -AllowDestructiveEdit=0 -Strict=0 \
 *       [-WorkOnCopy=/Game/BPParserScratch/Copy]  -unattended -nop4
 *
 * Exit codes: 0 = success, 10 = partial, 20 = failed, 30 = bad input, 40 = rolled_back.
 */
UCLASS()
class UBPATEditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
