// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Stable error codes shared between UE-side commandlet exit codes
 * and Python-side tool responses. Keep in sync with
 * agent_tools/schemas/error_codes.schema.json and docs/agent_tools.md.
 */
namespace BPATErrorCodes
{
	// Asset / IR
	constexpr const TCHAR* AssetNotFound          = TEXT("E1001");
	constexpr const TCHAR* IRNotDumped            = TEXT("E1002");
	constexpr const TCHAR* SchemaVersionMismatch  = TEXT("E1003");
	constexpr const TCHAR* NodeNotFound           = TEXT("E1101");
	constexpr const TCHAR* VariableNotFound       = TEXT("E1102");
	constexpr const TCHAR* SliceDepthExceeded     = TEXT("E1201");

	// Read-only safety
	constexpr const TCHAR* ReadOnlyViolation      = TEXT("E2001");
	constexpr const TCHAR* OutputDirInsideProject = TEXT("E2002");
	constexpr const TCHAR* OverwriteBlocked       = TEXT("E2003");

	// UE
	constexpr const TCHAR* UELaunchFailed         = TEXT("E5001");
	constexpr const TCHAR* UETimeout              = TEXT("E5002");
}

namespace BPATExitCodes
{
	constexpr int32 AllSuccess           = 0;
	constexpr int32 PartialSuccess       = 10;
	constexpr int32 FatalAssetFailures   = 20;
	constexpr int32 InvalidArguments     = 30;
	constexpr int32 ReadOnlyViolation    = 40;
	constexpr int32 InternalError        = 50;
}
