// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATPinIR.h"

namespace BPATEdgeKind
{
	constexpr const TCHAR* Exec               = TEXT("exec");
	constexpr const TCHAR* Data               = TEXT("data");
	constexpr const TCHAR* Delegate           = TEXT("delegate");
	constexpr const TCHAR* LatentContinuation = TEXT("latent_continuation");
	constexpr const TCHAR* Unknown            = TEXT("unknown");
}

/** Edge IR. Mirrors blueprint_ir_schema.md section 6. */
struct FBPATEdgeIR
{
	FString EdgeId;

	FString FromNodeId;
	FString FromPinId;
	FString ToNodeId;
	FString ToPinId;

	FString EdgeKind;            // BPATEdgeKind::*
	FBPATPinTypeInfo TypeInfo;

	bool bIsLatentContinuation = false;

	TSharedPtr<FJsonObject> Metadata;

	/** Reverse-phase reservation. */
	TSharedPtr<FJsonObject> CreationOrderHint;
};
