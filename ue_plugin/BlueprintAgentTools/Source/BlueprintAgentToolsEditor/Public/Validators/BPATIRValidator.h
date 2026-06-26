// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATBlueprintIR.h"

struct FBPATValidationIssue
{
	FString Code;          // BP_E_* / BP_W_*
	FString Level;         // "error" / "warning"
	FString Message;
	FString GraphId;
	FString NodeId;
};

struct FBPATValidationReport
{
	TArray<FBPATValidationIssue> Errors;
	TArray<FBPATValidationIssue> Warnings;
	bool IsOk() const { return Errors.Num() == 0; }
};

/** Atomic capability #13. Structural validation. See docs/validation_strategy.md §1. */
class FBPATIRValidator
{
public:
	static FBPATValidationReport Validate(const FBPATBlueprintIR& IR);
};
