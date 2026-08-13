// Copyright Grok Exodus. All Rights Reserved.

#include "GXPresentation.h"

DEFINE_LOG_CATEGORY(LogGXPresentation);

void FGXPresentationModule::StartupModule()
{
	UE_LOG(LogGXPresentation, Log, TEXT("GXPresentation started"));
}

void FGXPresentationModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGXPresentationModule, GXPresentation)
