// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FBPGenAssetResult;

/**
 * Builders for the shared support assets that the test blueprints depend on:
 *   E_BPParserTestState (UserDefinedEnum)
 *   ST_BPParserTestData (UserDefinedStruct)
 *   BPI_BPParserTest    (Blueprint Interface)
 *   BP_BPParserTargetActor     (Actor implementing the interface)
 *   BP_BPParserTestComponent   (ActorComponent)
 *
 * Each returns a result record; the orchestrator handles compile + save.
 */
class FBPGenSupportAssets
{
public:
	static FBPGenAssetResult BuildEnum();
	static FBPGenAssetResult BuildStruct();
	static FBPGenAssetResult BuildInterface();
	static FBPGenAssetResult BuildTargetActor();
	static FBPGenAssetResult BuildTestComponent();

	// Well-known asset paths (single source of truth).
	static const TCHAR* PathEnum()        { return TEXT("/Game/BPParserTest/E_BPParserTestState"); }
	static const TCHAR* PathStruct()      { return TEXT("/Game/BPParserTest/ST_BPParserTestData"); }
	static const TCHAR* PathInterface()   { return TEXT("/Game/BPParserTest/BPI_BPParserTest"); }
	static const TCHAR* PathTargetActor() { return TEXT("/Game/BPParserTest/BP_BPParserTargetActor"); }
	static const TCHAR* PathComponent()   { return TEXT("/Game/BPParserTest/BP_BPParserTestComponent"); }
};
