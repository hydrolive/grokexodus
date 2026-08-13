// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelHUD.h"
#include "GXLoadScreen.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

void AVoxelHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || GXLoadScreen::IsSlateOverlayActive())
	{
		return;
	}

	AGXVoxelWorld* WorldActor = nullptr;
	if (GetWorld())
	{
		for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
		{
			WorldActor = *It;
			break;
		}
	}

	static float Overlay = 1.0f;
	static float Elapsed = 0.0f;
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	Elapsed += Dt;
	const bool bReady = WorldActor && WorldActor->IsWorldReady();
	Overlay = GXLoadScreen::Draw(this, Canvas, Overlay, bReady, 2.5f, Elapsed, 0.9f, Dt, WorldActor);
	GXLoadScreen::DrawVersionStrip(Canvas, WorldActor ? WorldActor->GetLoadStatus() : FString(TEXT("VoxelHUD fallback")));
}
