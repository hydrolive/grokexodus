// Copyright Grok Exodus. All Rights Reserved.

#include "GXCore.h"

DEFINE_LOG_CATEGORY(LogGXCore);

void FGXCoreModule::StartupModule()
{
	UE_LOG(LogGXCore, Log, TEXT("GXCore started"));
}

void FGXCoreModule::ShutdownModule()
{
	UE_LOG(LogGXCore, Log, TEXT("GXCore shutdown"));
}

IMPLEMENT_MODULE(FGXCoreModule, GXCore)
