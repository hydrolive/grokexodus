// Copyright Grok Exodus. All Rights Reserved.
// Toggleable system traces. On while a feature is new; off when it is stable.
#pragma once

#include "CoreMinimal.h"

GXCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGXPerf, Log, All);

/** gx.perf.trace: 0=off, 1=systems (default while crust is in flux), 2=verbose. */
GXCORE_API int32 GXPerfLevel();

#define GX_PERF(Level, Fmt, ...) \
	do { if (GXPerfLevel() >= (Level)) { UE_LOG(LogGXPerf, Warning, Fmt, ##__VA_ARGS__); } } while (0)
