// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATPinIR.h"

namespace BPATParseStatus
{
	constexpr const TCHAR* Success = TEXT("success");
	constexpr const TCHAR* Partial = TEXT("partial");
	constexpr const TCHAR* Fatal   = TEXT("fatal");
}

struct FBPATSourceFingerprint
{
	int64   UAssetSize = 0;
	FString UAssetMTime;     // ISO 8601 UTC
	FString UAssetSha256;
};

struct FBPATAssetInfo
{
	FString AssetPath;
	FString PackagePath;
	FString AssetName;

	FString BlueprintClass;        // "Blueprint" / "WidgetBlueprint" / ...
	FString GeneratedClass;
	FString ParentClass;
	FString BlueprintType;         // BPTYPE_Normal / BPTYPE_Interface / ...
	FString EngineVersion;

	TArray<FString> PluginDependencies;

	FBPATSourceFingerprint SourceFingerprint;

	FString ParseTime;             // ISO 8601 UTC
	FString ParseStatus;           // BPATParseStatus::*

	TArray<TSharedPtr<FJsonObject>> Warnings;
	TArray<TSharedPtr<FJsonObject>> Errors;
};

struct FBPATVariableIR
{
	FString Name;
	FString Guid;
	FBPATPinTypeInfo Type;
	FString DefaultValue;
	bool bIsReplicated = false;
	FString Category;
};

struct FBPATFunctionIR
{
	FString Name;
	FString GraphId;
	bool bIsPure = false;
	bool bIsConst = false;
};

struct FBPATInterfaceImplIR
{
	FString InterfaceClassPath;
};

struct FBPATTimelineIR
{
	FString Name;
};

struct FBPATComponentNodeIR
{
	FString NodeId;                          // "scs_<idx>"
	FString Name;
	FString ComponentClassPath;
	TArray<FBPATComponentNodeIR> Children;
};

struct FBPATComponentTreeIR
{
	bool bHasComponents = false;
	FBPATComponentNodeIR Root;
};

struct FBPATMemberSet
{
	TArray<FBPATVariableIR>      Variables;
	TArray<FBPATFunctionIR>      Functions;
	TArray<FBPATFunctionIR>      Macros;
	TArray<FBPATFunctionIR>      EventDispatchers;
	TArray<FBPATInterfaceImplIR> ImplementedInterfaces;
	TArray<FBPATTimelineIR>      Timelines;
};
