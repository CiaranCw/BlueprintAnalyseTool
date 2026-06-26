// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATNodeIR.h"
#include "Schema/BPATEdgeIR.h"

namespace BPATGraphType
{
	constexpr const TCHAR* Ubergraph    = TEXT("ubergraph");
	constexpr const TCHAR* Function     = TEXT("function");
	constexpr const TCHAR* Macro        = TEXT("macro");
	constexpr const TCHAR* Delegate     = TEXT("delegate");
	constexpr const TCHAR* Intermediate = TEXT("intermediate");
	constexpr const TCHAR* Widget       = TEXT("widget");
	constexpr const TCHAR* Anim         = TEXT("anim");
	constexpr const TCHAR* Unknown      = TEXT("unknown");
}

/** Local variable inside a function graph. */
struct FBPATLocalVariableIR
{
	FString Name;
	FBPATPinTypeInfo Type;
	FString DefaultValue;
};

/** Graph IR. Mirrors blueprint_ir_schema.md section 3. */
struct FBPATGraphIR
{
	FString GraphId;
	FString GraphName;
	FString GraphType;            // BPATGraphType::*
	FString GraphOwner;           // owning blueprint asset path

	int32 NodeCount = 0;
	int32 PinCount = 0;
	int32 EdgeCount = 0;

	TArray<FString> EntryNodeIds;

	TArray<FBPATLocalVariableIR> LocalVariables;

	TArray<FBPATNodeIR> Nodes;
	TArray<FBPATEdgeIR> Edges;

	TArray<TSharedPtr<FJsonObject>> Warnings;
};
