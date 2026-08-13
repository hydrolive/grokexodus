// Copyright Grok Exodus. All Rights Reserved.

#include "GXCelestial.h"

DEFINE_LOG_CATEGORY(LogGXCelestial);

void FGXCelestialModule::StartupModule()
{
	UE_LOG(LogGXCelestial, Log, TEXT("GXCelestial started"));
}

void FGXCelestialModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGXCelestialModule, GXCelestial)
