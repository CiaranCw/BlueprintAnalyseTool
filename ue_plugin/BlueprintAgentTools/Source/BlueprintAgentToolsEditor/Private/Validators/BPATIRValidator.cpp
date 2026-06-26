// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Validators/BPATIRValidator.h"
#include "BPATLog.h"

// TODO(M1): implement structural validation per docs/validation_strategy.md §1.

FBPATValidationReport FBPATIRValidator::Validate(const FBPATBlueprintIR& /*IR*/)
{
	FBPATValidationReport Report;
	UE_LOG(LogBPAT, Warning, TEXT("BPATIRValidator::Validate not implemented yet."));
	return Report;
}
