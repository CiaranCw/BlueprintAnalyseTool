// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace BPATGlobals
{
	/** IR JSON schema version. Keep in sync with agent_tools/schemas/*.schema.json. */
	constexpr const TCHAR* SchemaVersion = TEXT("0.1.0");

	/** Default thresholds for layered output. */
	constexpr int32 DefaultInlineNodeLimitPerGraph = 200;
	constexpr int32 DefaultFullSplitTotalNodes     = 1000;

	/** Package path prefixes that must NEVER be written / saved during dump. */
	inline const TArray<FString>& GetProtectedPackagePrefixes()
	{
		static const TArray<FString> Prefixes = {
			TEXT("/Game/"),
			TEXT("/Engine/"),
			TEXT("/Script/"),
			TEXT("/Config/"),
		};
		return Prefixes;
	}
}
