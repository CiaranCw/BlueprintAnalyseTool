// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Readers/BPATAssetEnumerator.h"
#include "BPATLog.h"

// TODO(M1): implement using IAssetRegistry. Needs V2 verification on
// FTopLevelAssetPath usage in UE 5.4.

TArray<FBPATAssetEnumerationEntry> FBPATAssetEnumerator::Enumerate(
	const FBPATAssetEnumerationFilter& /*Filter*/)
{
	UE_LOG(LogBPAT, Warning, TEXT("BPATAssetEnumerator::Enumerate not implemented yet."));
	return {};
}

bool FBPATAssetEnumerator::LookupSingle(const FString& /*AssetPath*/,
                                         FBPATAssetEnumerationEntry& /*OutEntry*/)
{
	UE_LOG(LogBPAT, Warning, TEXT("BPATAssetEnumerator::LookupSingle not implemented yet."));
	return false;
}
