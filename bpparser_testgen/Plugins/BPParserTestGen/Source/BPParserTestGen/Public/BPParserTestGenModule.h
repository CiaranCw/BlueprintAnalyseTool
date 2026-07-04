// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPParserTestGen, Log, All);

class FBPAgentLiveService;

class FBPParserTestGenModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** editor_live service accessor (may be null in commandlet/headless runs). */
	static TSharedPtr<FBPAgentLiveService> GetLiveService();

private:
	void RegisterMenus();
	void OnGenerateClicked();

	TArray<IConsoleObject*> ConsoleCommands;
	TSharedPtr<FBPAgentLiveService> LiveService;
};
