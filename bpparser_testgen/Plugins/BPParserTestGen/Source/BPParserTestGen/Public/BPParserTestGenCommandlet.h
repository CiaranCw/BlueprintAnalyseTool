// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPParserTestGenCommandlet.generated.h"

/**
 * Commandlet entry point.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=BPParserTestGen [-NoSave]
 *
 * Generates all /Game/BPParserTest assets and writes a generation report to
 * <Project>/Saved/BPParserTestReports/generation_log.json.
 */
UCLASS()
class UBPParserTestGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
