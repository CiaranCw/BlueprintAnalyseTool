// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Readers/BPATBlueprintLoader.h"
#include "BPATLog.h"
#include "BPATErrorCodes.h"

// TODO(M1): implement via LoadObject<UBlueprint>(nullptr, *PackagePath).
// Make sure no Modify() / MarkPackageDirty() is invoked in PostLoad chain.

FBPATBlueprintLoadResult FBPATBlueprintLoader::Load(const FString& PackagePath)
{
	FBPATBlueprintLoadResult Result;
	Result.ErrorCode = BPATErrorCodes::AssetNotFound;
	Result.ErrorMessage = FString::Printf(
		TEXT("BPATBlueprintLoader::Load not implemented yet. (path=%s)"), *PackagePath);
	UE_LOG(LogBPAT, Warning, TEXT("%s"), *Result.ErrorMessage);
	return Result;
}
