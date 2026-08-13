// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelHUD.h"
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

	Line1 = TEXT("GX  |  LMB drill/place  |  RMB mode  |  R material  |  F5 save");
	Line2 = TEXT("Load Lvl_VoxelPlanet  ·  no bunker / no walker");
	Line3 = TEXT("Mouse up = sky. Ground should be under your feet.");

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
