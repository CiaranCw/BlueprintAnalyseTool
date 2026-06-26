// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Exporters/BPATIRSerializer.h"
#include "Util/BPATPathPolicy.h"
#include "BPATLog.h"

// TODO(M1): write manifest.json + graphs/<id>.summary.json + (optional) full/nodes split.
// Every write must go through FBPATPathPolicy::AssertWritable first.

FBPATSerializeResult FBPATIRSerializer::WriteAll(const FBPATBlueprintIR& /*IR*/,
                                                  const FBPATOutputLayout& /*Layout*/,
                                                  const FBPATSerializeOptions& /*Options*/)
{
	FBPATSerializeResult Result;
	UE_LOG(LogBPAT, Warning, TEXT("BPATIRSerializer::WriteAll not implemented yet."));
	return Result;
}
