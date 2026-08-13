// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelHUD.h"
#include "Voxel/VoxelExodusCharacter.h"
#include "Voxel/VoxelTerrainToolComponent.h"
#include "Voxel/VoxelCraftsmanshipComponent.h"
#include "Voxel/VoxelWalkerPawn.h"
#include "Voxel/VoxelPlanetActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void AVoxelHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	APawn* Pawn = PlayerOwner->GetPawn();
	if (!Pawn)
	{
		return;
	}

	FString Line1, Line2, Line3;

	if (const AVoxelWalkerPawn* Walker = Cast<AVoxelWalkerPawn>(Pawn))
	{
		Line1 = TEXT("WALKER  [W/S drive] [A/D steer] [F eject+cargo] [X destroy+lose cargo]");
		Line2 = FString::Printf(TEXT("Feet:%s  %s"),
			Walker->bFeetOnTerrain ? TEXT("YES") : TEXT("no"), *Walker->GetCargoStatusLine());
		Line3 = TEXT("Pillar: lose walker/cargo — bunkers & planet stay.");
	}
	else if (const AVoxelExodusCharacter* VC = Cast<AVoxelExodusCharacter>(Pawn))
	{
		const UVoxelTerrainToolComponent* Tool = VC->TerrainTool;
		const UVoxelCraftsmanshipComponent* Craft = VC->FindComponentByClass<UVoxelCraftsmanshipComponent>();
		const FString Mode = (Tool && Tool->Mode == EVoxelToolMode::Place) ? TEXT("PLACE") : TEXT("DRILL");
		const int32 Mat = Tool ? Tool->PlaceMaterialId : 0;
		Line1 = FString::Printf(TEXT("Tool: %s  Mat:%d  [LMB dig/place] [RMB mode] [R mat] [B claim bunker] [V walker] [F5 save]"),
			*Mode, Mat);
		Line2 = Craft ? Craft->GetStatusLine() : TEXT("Craftsmanship: n/a");
		int32 Loaded = 0, Dirty = 0;
		int64 Mem = 0;
		float MeshMs = 0.f;
		for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
		{
			It->GetStreamingStats(Loaded, Dirty, Mem, MeshMs);
			Line3 = FString::Printf(TEXT("Planet chunks:%d dirty:%d mesh:%.1fms  [Bunker claim protects digs]"),
				Loaded, Dirty, MeshMs);
			break;
		}
	}
	else
	{
		Line1 = TEXT("Grok Exodus — Voxel World");
	}

	const float X = 24.f;
	float Y = 24.f;
	const FLinearColor Col(0.85f, 0.95f, 0.75f, 1.f);
	if (GEngine)
	{
		// Prefer canvas text
	}
	DrawText(Line1, Col, X, Y, nullptr, 1.15f, false);
	Y += 22.f;
	DrawText(Line2, Col, X, Y, nullptr, 1.1f, false);
	Y += 22.f;
	DrawText(Line3, FLinearColor(0.7f, 0.85f, 1.f, 1.f), X, Y, nullptr, 1.05f, false);
}
