// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BlueprintAgentToolsEditorModule.h"
#include "BPATLog.h"

#define LOCTEXT_NAMESPACE "FBlueprintAgentToolsEditorModule"

DEFINE_LOG_CATEGORY(LogBPAT);

void FBlueprintAgentToolsEditorModule::StartupModule()
{
	UE_LOG(LogBPAT, Log, TEXT("BlueprintAgentToolsEditor module started (read-only mode)."));
}

void FBlueprintAgentToolsEditorModule::ShutdownModule()
{
	UE_LOG(LogBPAT, Log, TEXT("BlueprintAgentToolsEditor module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintAgentToolsEditorModule, BlueprintAgentToolsEditor)
