// Copyright Grok Exodus. All Rights Reserved.

#include "GXCore.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogGXCore);
DEFINE_LOG_CATEGORY(LogGXPerf);

static TAutoConsoleVariable<int32> CVarGXPerfTrace(
	TEXT("gx.perf.trace"),
	1,
	TEXT("GX performance traces: 0=off, 1=systems, 2=verbose. Turn off when a system is stable."),
	ECVF_Default);

int32 GXPerfLevel()
{
	return CVarGXPerfTrace.GetValueOnAnyThread();
}

void FGXCoreModule::StartupModule()
{
	UE_LOG(LogGXCore, Log, TEXT("GXCore started"));
}

void FGXCoreModule::ShutdownModule()
{
	UE_LOG(LogGXCore, Log, TEXT("GXCore shutdown"));
}

IMPLEMENT_MODULE(FGXCoreModule, GXCore)
