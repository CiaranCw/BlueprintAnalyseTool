// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** IR-side type info for a single pin. Keep aligned with PinType in IR JSON. */
struct FBPATPinTypeInfo
{
	FString Category;          // exec / object / struct / real / int / byte / string / wildcard / ...
	FString SubCategory;
	FString ObjectType;        // PinSubCategoryObject->GetPathName()
	FString ContainerType;     // none / array / set / map
};

/** Reference to another pin (used in linked_to). */
struct FBPATPinLink
{
	FString NodeId;
	FString PinId;
};

/** Pin IR. Mirrors blueprint_ir_schema.md section 5. */
struct FBPATPinIR
{
	FString PinId;
	FString PinName;
	FString Direction;         // input / output

	FBPATPinTypeInfo TypeInfo;

	bool bIsExec = false;
	bool bIsData = false;

	FString DefaultValue;
	FString DefaultObject;     // path or empty

	TArray<FBPATPinLink> LinkedTo;

	/** Reverse-phase reservation. Always null in current phase. */
	TSharedPtr<FJsonObject> ReverseHints;
};
