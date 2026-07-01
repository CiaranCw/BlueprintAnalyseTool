// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPCreateCommandlet.generated.h"

/**
 * Spec-driven Blueprint creation (Create task of the external-AI-callable agent).
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=BPCreate \
 *       -SpecFile="<create request json>" -OutputDir="<dir>" -unattended -nop4
 *
 * Outputs manifest.json / created_ir.json / create_result.json / summary.md / viz/created.dot.
 * Exit codes: 0 success, 10 partial, 20 failed, 30 bad input, 41 exists (overwrite refused).
 */
UCLASS()
class UBPCreateCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
