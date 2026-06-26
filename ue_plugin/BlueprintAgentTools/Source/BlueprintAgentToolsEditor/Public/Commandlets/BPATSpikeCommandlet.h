// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BPATSpikeCommandlet.generated.h"

/**
 * One-off verification commandlet for the 8 待验证 items in docs/architecture.md §7.
 *
 *   UnrealEditor-Cmd.exe MyTestProject.uproject -run=BPATSpike -OutputDir=D:\bpat_spike
 *
 * Optional:
 *   -AssetPath=/Game/Spike/BP_Hero
 *   -WidgetAssetPath=/Game/Spike/WBP_HUD
 *   -AnimAssetPath=/Game/Spike/ABP_Mannequin
 *   -LevelMaps=/Game/Maps/M_Test  (V7 only)
 *
 * Output: <OutputDir>/spike/v*.json + summary.json. See docs/spike_ue54.md.
 */
UCLASS()
class UBPATSpikeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UBPATSpikeCommandlet();
	virtual int32 Main(const FString& Params) override;
};
