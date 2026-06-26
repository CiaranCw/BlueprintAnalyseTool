// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPParserTestDumpCommandlet.generated.h"

/**
 * Dumps a single blueprint's IR to JSON (for regression diffing).
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=BPParserTestDump \
 *       -AssetPath=/Game/BPParserTest/BP_01_PrimitivePins_Basic \
 *       -OutputDir=<dir>
 *
 * Exit codes: 0 = ok, 30 = bad args, 50 = dump failed.
 */
UCLASS()
class UBPParserTestDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
