// Copyright Grok Exodus. All Rights Reserved.

#include "GXHUDLayout.h"
#include "GXLoadScreen.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

AGXHUDLayout::AGXHUDLayout()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AGXHUDLayout::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
}

void AGXHUDLayout::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || GXLoadScreen::IsSlateOverlayActive())
	{
		return;
	}

	if (UWorld* W = GetWorld())
	{
		Elapsed += W->GetDeltaSeconds();
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

	const bool bReady = WorldActor && WorldActor->IsWorldReady();
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	OverlayAlpha = GXLoadScreen::Draw(this, Canvas, OverlayAlpha, bReady, MinHoldSeconds, Elapsed, FadeSeconds, Dt, WorldActor);

	const FString Extra = WorldActor
		? FString::Printf(TEXT("%s  %.0f%%"), *WorldActor->GetLoadStatus(), WorldActor->GetLoadProgress() * 100.f)
		: FString(TEXT("no AGXVoxelWorld"));
	GXLoadScreen::DrawVersionStrip(Canvas, Extra);

	if (OverlayAlpha <= 0.01f && bDrawDebugStrip && GEngine && GEngine->GetSmallFont())
	{
		Canvas->SetDrawColor(FColor(220, 230, 240));
		Canvas->DrawText(GEngine->GetSmallFont(),
			TEXT("LMB drill/place  |  RMB mode  |  R material  |  F5 save"),
			16.f, 36.f, 1.1f, 1.1f);
	}
}
