// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FBPGenAssetResult;

/** Builders for the 10 primary test blueprints + the negative-case blueprint. */
class FBPGenTestBlueprints
{
public:
	static FBPGenAssetResult Build_BP01_PrimitivePins();
	static FBPGenAssetResult Build_BP02_StructEnumContainers();
	static FBPGenAssetResult Build_BP03_ObjectRefCastInterface();
	static FBPGenAssetResult Build_BP04_ExecFlowControl();
	static FBPGenAssetResult Build_BP05_FunctionsMacrosLocals();
	static FBPGenAssetResult Build_BP06_DelegatesDispatchers();
	static FBPGenAssetResult Build_BP07_LatentTimerAsync();
	static FBPGenAssetResult Build_BP08_ComplexGameplay();
	static FBPGenAssetResult Build_BP09_FormattingCommentsReroutes();
	static FBPGenAssetResult Build_BP10_RoundTripMaster();
	static FBPGenAssetResult Build_BP11_SupplementalCoverage();
	static FBPGenAssetResult Build_BP99_NegativeEdgeCases();

	static const TCHAR* Path01() { return TEXT("/Game/BPParserTest/BP_01_PrimitivePins_Basic"); }
	static const TCHAR* Path02() { return TEXT("/Game/BPParserTest/BP_02_StructEnumContainers"); }
	static const TCHAR* Path03() { return TEXT("/Game/BPParserTest/BP_03_ObjectReference_Cast_Interface"); }
	static const TCHAR* Path04() { return TEXT("/Game/BPParserTest/BP_04_ExecFlow_Control"); }
	static const TCHAR* Path05() { return TEXT("/Game/BPParserTest/BP_05_Functions_Macros_LocalVariables"); }
	static const TCHAR* Path06() { return TEXT("/Game/BPParserTest/BP_06_Delegates_EventDispatchers"); }
	static const TCHAR* Path07() { return TEXT("/Game/BPParserTest/BP_07_Latent_Timeline_Async"); }
	static const TCHAR* Path08() { return TEXT("/Game/BPParserTest/BP_08_ComplexGameplayLikeGraph"); }
	static const TCHAR* Path09() { return TEXT("/Game/BPParserTest/BP_09_NodeFormatting_Comments_Reroutes"); }
	static const TCHAR* Path10() { return TEXT("/Game/BPParserTest/BP_10_ParserRoundTrip_Master"); }
	static const TCHAR* Path11() { return TEXT("/Game/BPParserTest/BP_11_SupplementalCoverage"); }
	static const TCHAR* Path99() { return TEXT("/Game/BPParserTest/BP_99_NegativeOrEdgeCases"); }
};
