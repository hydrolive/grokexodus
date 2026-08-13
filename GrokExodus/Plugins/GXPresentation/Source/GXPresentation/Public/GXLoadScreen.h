// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class AHUD;
class UCanvas;
class AGXVoxelWorld;

namespace GXLoadScreen
{
	/** Full-screen overlay + bar. Returns remaining overlay alpha. */
	GXPRESENTATION_API float Draw(
		AHUD* HUD,
		UCanvas* Canvas,
		float OverlayAlpha,
		bool bWorldReady,
		float MinHoldSeconds,
		float ElapsedSeconds,
		float FadeSeconds,
		float DeltaSeconds,
		AGXVoxelWorld* World);

	GXPRESENTATION_API void DrawVersionStrip(UCanvas* Canvas, const FString& Extra);

	/** Slate viewport overlay owns the boot UI; HUD canvas becomes a no-op. */
	GXPRESENTATION_API bool IsSlateOverlayActive();
	GXPRESENTATION_API void SetSlateOverlayActive(bool bActive);
}
