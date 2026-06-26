// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATEdgeIR.h"
#include "Schema/BPATNodeIR.h"

/** Atomic capability #7. */
class FBPATEdgeResolver
{
public:
	struct FResult
	{
		TArray<FBPATEdgeIR> Edges;
		TArray<TSharedPtr<FJsonObject>> Warnings;
	};

	/**
	 * Resolves Pin LinkedTo references into typed edges.
	 *
	 * V3 (待验证): Latent edge classification via UFunction "Latent" metadata
	 * for K2Node_CallFunction. Other latent-like nodes fall back to exec
	 * with metadata.is_latent_continuation = null + warning.
	 */
	static FResult Resolve(const TArray<FBPATNodeIR>& Nodes);
};
