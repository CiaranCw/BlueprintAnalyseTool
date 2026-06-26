// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPParserTestGenModule.h"
#include "BPGenOrchestrator.h"
#include "ToolMenus.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "BPParserTestGen"

DEFINE_LOG_CATEGORY(LogBPParserTestGen);

void FBPParserTestGenModule::StartupModule()
{
	UE_LOG(LogBPParserTestGen, Log, TEXT("BPParserTestGen module starting up."));

	// Console command: BPParserTest.Generate
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BPParserTest.Generate"),
		TEXT("Generate the /Game/BPParserTest blueprint test suite."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FBPGenOrchestrator::GenerateAll();
		}),
		ECVF_Default));

	// Editor menu button (Tools menu). Guarded so commandlet/headless runs don't crash.
	if (!IsRunningCommandlet() && UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPParserTestGenModule::RegisterMenus));
	}
}

void FBPParserTestGenModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	for (IConsoleObject* Cmd : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
	ConsoleCommands.Empty();
}

void FBPParserTestGenModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("BPParserTest");
	Section.Label = LOCTEXT("BPParserTestSection", "BP Parser Test");
	Section.AddMenuEntry(
		"GenerateBPParserTests",
		LOCTEXT("GenerateLabel", "Generate BP Parser Test Suite"),
		LOCTEXT("GenerateTooltip", "Procedurally (re)generate all /Game/BPParserTest assets."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBPParserTestGenModule::OnGenerateClicked)));
}

void FBPParserTestGenModule::OnGenerateClicked()
{
	const FBPGenReport Report = FBPGenOrchestrator::GenerateAll();

	const FText Msg = FText::FromString(FString::Printf(
		TEXT("BP Parser Test generation finished.\n\nAssets attempted: %d\nCompiled OK: %d\nWith warnings: %d\nFailed: %d\n\nReport: %s"),
		Report.TotalAssets, Report.CompiledOk, Report.CompiledWithWarnings, Report.Failed, *Report.ReportFilePath));

	FMessageDialog::Open(EAppMsgType::Ok, Msg);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPParserTestGenModule, BPParserTestGen)
