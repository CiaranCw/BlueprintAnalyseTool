// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Builder/BPATIRBuilder.h"
#include "BPATGlobals.h"
#include "BPATLog.h"

// TODO(M1): drive readers in fixed order. See docs/atomic_capabilities.md §4.

FBPATBlueprintIR FBPATIRBuilder::Build(const UBlueprint* /*Blueprint*/)
{
	FBPATBlueprintIR IR;
	IR.SchemaVersion = BPATGlobals::SchemaVersion;
	UE_LOG(LogBPAT, Warning, TEXT("BPATIRBuilder::Build not implemented yet."));
	return IR;
}
