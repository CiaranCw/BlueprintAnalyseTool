// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPCreate: spec-driven Blueprint creation from a structured JSON request (the Create task of the
// external-AI-callable Blueprint Agent). Reuses FBPGen for all asset/member/node operations, then
// compiles + saves, re-dumps the created IR, and writes machine-readable create_result/manifest.
// Honours overwrite_policy; never overwrites unless explicitly allowed.
#pragma once

#include "CoreMinimal.h"

class FBPCreate
{
public:
	// SpecFile = JSON matching the "create" request.request schema (docs/request_schemas.md).
	// Returns: 0 success, 10 partial, 20 failed, 30 bad input, 41 exists (overwrite refused).
	static int32 Run(const FString& SpecFile, const FString& OutputDir);
};
