// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPATDumpCommandlet.generated.h"

/**
 * Atomic capability #17. Entry point: UnrealEditor-Cmd <Project> -run=BPATDump ...
 *
 * Required args:
 *   -AssetPath=/Game/...           OR -ProjectScan=1
 *   -OutputDir=<absolute path outside project>
 *
 * Optional:
 *   -ClassFilter=Blueprint,WidgetBlueprint,AnimBlueprint
 *   -Layers=manifest,graphs,nodes
 *   -OverwritePolicy=skip|overwrite|fail   (default: skip)
 *   -DryRun=0|1                            (default: 0)
 *   -StrictReadOnly=0|1                    (default: 1; phase-1 forbids 0)
 *   -LogJson=<absolute path under OutputDir>
 *
 * Exit codes: see BPATErrorCodes.h / docs/agent_tools.md §4.
 */
UCLASS()
class UBPATDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UBPATDumpCommandlet();
	virtual int32 Main(const FString& Params) override;
};
