// Copyright Grok Exodus. All Rights Reserved.

#include "GXConstruct.h"

DEFINE_LOG_CATEGORY(LogGXConstruct);

void FGXConstructModule::StartupModule()
{
	UE_LOG(LogGXConstruct, Log, TEXT("GXConstruct started"));
}

void FGXConstructModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGXConstructModule, GXConstruct)
