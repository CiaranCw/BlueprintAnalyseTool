// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Minimal, self-contained Blueprint -> IR JSON dumper. Produces output aligned
// with deliverables/expected_ir/*.json so it can be used for regression diffing.
// This is intentionally independent of the (skeleton) BlueprintAgentTools serializer.
#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class FJsonObject;

class FBPGenIRDumper
{
public:
	/** Build the IR object for a loaded blueprint. */
	static TSharedPtr<FJsonObject> DumpBlueprint(UBlueprint* BP);

	/** Load by asset path (e.g. /Game/BPParserTest/BP_01_PrimitivePins_Basic), dump to OutDir/<name>.ir.json.
	 *  Returns the written file path, or empty on failure. */
	static FString DumpAssetToFile(const FString& AssetPath, const FString& OutDir);

	/** Toggle inclusion of the (potentially large) per-widget discovery arrays in the Widget IR.
	 *  Both default to true; set false to omit `settable_properties`/`slot_settable_properties` or
	 *  `bindable_events` for large WBPs. Maps to request `include.widget_settable_properties` /
	 *  `include.widget_bindable_events`. */
	static void SetWidgetIncludeOptions(bool bIncludeSettableProps, bool bIncludeBindableEvents);
};
