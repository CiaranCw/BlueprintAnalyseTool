// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPParserTestGenModule.h"
#include "BPGenOrchestrator.h"
#include "BPAgentLiveService.h"
#include "ToolMenus.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "BPParserTestGen"

DEFINE_LOG_CATEGORY(LogBPParserTestGen);

namespace
{
	// Weak handle so the console commands / static accessor can reach the running service.
	TWeakPtr<FBPAgentLiveService> GBPAgentLiveServiceWeak;
}

TSharedPtr<FBPAgentLiveService> FBPParserTestGenModule::GetLiveService()
{
	return GBPAgentLiveServiceWeak.Pin();
}

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

	// ---- editor_live service (file-queue). Interactive editor only; NEVER in a commandlet
	// (native_full runs commandlets directly and must not race the queue). ----
	if (GIsEditor && !IsRunningCommandlet())
	{
		LiveService = MakeShared<FBPAgentLiveService>();
		GBPAgentLiveServiceWeak = LiveService;
		LiveService->Start();

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("BPAgent.Live.Status"),
			TEXT("Print the BPAgentLiveService (editor_live) status line."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				if (TSharedPtr<FBPAgentLiveService> S = FBPParserTestGenModule::GetLiveService())
				{ UE_LOG(LogBPParserTestGen, Display, TEXT("%s"), *S->GetStatusLine()); }
				else { UE_LOG(LogBPParserTestGen, Display, TEXT("BPAgentLiveService not running.")); }
			}),
			ECVF_Default));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("BPAgent.Live.Start"),
			TEXT("Start the BPAgentLiveService (editor_live) request-queue poller."),
			FConsoleCommandDelegate::CreateLambda([]()
			{ if (TSharedPtr<FBPAgentLiveService> S = FBPParserTestGenModule::GetLiveService()) { S->Start(); } }),
			ECVF_Default));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("BPAgent.Live.Stop"),
			TEXT("Stop the BPAgentLiveService (editor_live) request-queue poller."),
			FConsoleCommandDelegate::CreateLambda([]()
			{ if (TSharedPtr<FBPAgentLiveService> S = FBPParserTestGenModule::GetLiveService()) { S->Stop(); } }),
			ECVF_Default));
	}

	// Editor menu button (Tools menu). Guarded so commandlet/headless runs don't crash.
	if (!IsRunningCommandlet() && UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPParserTestGenModule::RegisterMenus));
	}
}

void FBPParserTestGenModule::ShutdownModule()
{
	if (LiveService.IsValid())
	{
		LiveService->Stop();
		LiveService.Reset();
	}
	GBPAgentLiveServiceWeak.Reset();

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
