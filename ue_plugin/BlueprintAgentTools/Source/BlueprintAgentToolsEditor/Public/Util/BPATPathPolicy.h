// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Centralized path policy. ALL filesystem writes by this plugin must go
 * through BPATPathPolicy::AssertWritable / OpenWriteFile.
 *
 * See docs/readonly_safety.md sections L1, L7.
 */
class FBPATPathPolicy
{
public:
	/** Configure the (single) allowed output directory for the current run. */
	static void SetOutputRoot(const FString& InOutputRootAbsolute,
	                          const FString& InProjectRootAbsolute);

	/**
	 * Abort (Fatal) if AbsPath is not absolute, not under the configured OutputRoot,
	 * or the OutputRoot itself is not isolated from the project / engine.
	 */
	static void AssertWritable(const FString& AbsPath);

	/** Returns true if the given /Game/... package path is read-only protected. */
	static bool IsProtectedPackagePath(const FString& PackagePath);

private:
	static FString OutputRootAbs;
	static FString ProjectRootAbs;
	static bool    bConfigured;
};
