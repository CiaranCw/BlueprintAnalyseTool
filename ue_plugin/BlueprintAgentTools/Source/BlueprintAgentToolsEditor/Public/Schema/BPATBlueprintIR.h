// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATAssetIR.h"
#include "Schema/BPATGraphIR.h"

/** Optional UWidgetBlueprint widget tree info. */
struct FBPATWidgetTreeIR
{
	bool bHasWidgetTree = false;
	TSharedPtr<FJsonObject> RootWidgetJson;   // serialized lazily
};

/** Optional UAnimBlueprint AnimGraph (partial). */
struct FBPATAnimGraphIR
{
	bool bHasAnimGraph = false;
	bool bPartial = true;
	FString PartialReason;
	TArray<FBPATNodeIR> Nodes;
	TArray<FBPATEdgeIR> Edges;
};

/** Top-level Blueprint IR. */
struct FBPATBlueprintIR
{
	FString SchemaVersion;
	FBPATAssetInfo Asset;
	FBPATMemberSet Members;
	FBPATComponentTreeIR ComponentTree;
	FBPATWidgetTreeIR    WidgetTree;
	FBPATAnimGraphIR     AnimGraph;

	TArray<FBPATGraphIR> Graphs;

	struct FComplexityStats
	{
		int32 GraphCount = 0;
		int32 TotalNodes = 0;
		int32 TotalEdges = 0;
		int32 MaxGraphDepth = 0;
	} Complexity;

	/** Reverse-phase reservation. Always default-initialized in current phase. */
	struct FReverseGenerationReservation
	{
		TSharedPtr<FJsonObject> ConstructionRecipe;
		TSharedPtr<FJsonObject> CompileOptions;
		TSharedPtr<FJsonObject> NodeCreationHints;
		TSharedPtr<FJsonObject> PinDefaultOverrides;
		bool bCurrentPhaseWritesThem = false;
	} ReverseGenerationReservation;
};
