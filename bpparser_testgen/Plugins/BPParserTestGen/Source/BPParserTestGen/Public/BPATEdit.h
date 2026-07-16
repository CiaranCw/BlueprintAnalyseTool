// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPATEdit: atomic, plan-based, verifiable, reversible Blueprint editing.
// Takes a structured edit request, builds an edit plan, (optionally) backs up the
// asset, applies atomic operations in a safe order, compiles + saves, re-dumps the
// IR, diffs against the baseline, and writes machine-readable artifacts for other
// agents. Non-destructive by default: destructive ops require AllowDestructiveEdit.
#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class FJsonObject;

class FBPATEdit
{
public:
	struct FOptions
	{
		FString Mode = TEXT("plan-only");   // plan-only | dry-run | apply | apply-and-verify
		bool bCreateBackup = true;
		bool bAllowDestructive = false;
		bool bStrict = false;
		FString WorkOnCopy;                 // optional /Game path: duplicate source here and edit the COPY
		FString OutputDir;                  // base output dir; a per-asset/timestamp subdir is created
		/** When set, apply fails with stale_plan if current baseline hash differs. */
		FString ExpectedBaselineIrHash;
		/** Run property preflight before apply (default true for apply modes). */
		bool bRunPreflight = true;
		bool bStrictPreflight = true;
	};

	/** Run the full pipeline. Returns a process exit code:
	 *  0 = success, 10 = partial, 20 = failed, 30 = bad input/precondition, 40 = rolled_back.
	 *  Always writes artifacts (plan/result/diff/...) under OutputDir when possible. */
	// OutStatus (optional) receives the precise result status string (success / success_with_warnings /
	// partial / rolled_back / stale_plan / failed) so callers can distinguish statuses that share an exit code.
	static int32 Run(const FString& AssetPath, const TSharedPtr<FJsonObject>& Request, const FOptions& Opt, FString* OutStatus = nullptr);

	/** True if an operation name mutates existing structure (needs AllowDestructiveEdit). */
	static bool IsDestructiveOperation(const FString& Op);
};
