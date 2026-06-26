// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATPinIR.h"

/** Optional named reference (function / variable / event / macro). */
struct FBPATMemberRef
{
	FString MemberName;
	FString MemberParent;     // class path
	bool    bIsSelfContext = false;
};

/** Node IR. Mirrors blueprint_ir_schema.md section 4. */
struct FBPATNodeIR
{
	FString NodeId;
	FGuid   NodeGuid;
	FString NodeClass;        // e.g. "K2Node_CallFunction"
	FString NodeTitle;
	FString NodeComment;

	FIntPoint NodePosition = FIntPoint::ZeroValue;
	FString  NodeEnabledState;
	FString  NodeTypeCategory;  // event / function_call / variable_get / cast / ...

	TOptional<FBPATMemberRef> FunctionReference;
	TOptional<FBPATMemberRef> VariableReference;
	TOptional<FBPATMemberRef> EventReference;
	TOptional<FBPATMemberRef> MacroReference;

	TArray<FBPATPinIR> Pins;

	TMap<FString, FString> Metadata;

	FString GraphId;
	FString OwnerAssetPath;

	/** Reverse-phase reservation. Always null in current phase. */
	TSharedPtr<FJsonObject> ReverseHints;
};
