// Copyright Grok Exodus. All Rights Reserved.

#include "GXLoadScreen.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "GameFramework/HUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

float GXLoadScreen::Draw(
	AHUD* HUD,
	UCanvas* Canvas,
	float OverlayAlpha,
	bool bWorldReady,
	float MinHoldSeconds,
	float ElapsedSeconds,
	float FadeSeconds,
	float DeltaSeconds,
	AGXVoxelWorld* World)
{
	if (!HUD || !Canvas)
	{
		return OverlayAlpha;
	}

	const bool bHold = ElapsedSeconds < MinHoldSeconds;
	if (bWorldReady && !bHold)
	{
		OverlayAlpha = FMath::Max(0.0f, OverlayAlpha - DeltaSeconds / FMath::Max(FadeSeconds, 0.05f));
	}
	else
	{
		OverlayAlpha = 1.0f;
	}

	if (OverlayAlpha <= 0.01f)
	{
		return 0.0f;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	HUD->DrawRect(FLinearColor(0.01f, 0.015f, 0.03f, OverlayAlpha), 0.f, 0.f, W, H);

	const float Progress = World ? FMath::Clamp(World->GetLoadProgress(), 0.02f, 1.0f) : 0.05f;
	const FString Status = World ? World->GetLoadStatus() : TEXT("Starting planet systems…");

	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;

	const uint8 A = static_cast<uint8>(FMath::Clamp(OverlayAlpha * 255.f, 1.f, 255.f));
	if (Large)
	{
		Canvas->SetDrawColor(FColor(240, 244, 250, A));
		Canvas->DrawText(Large, TEXT("GROK EXODUS"), 80.f, H * 0.34f, 2.2f, 2.2f);
	}
	if (Small)
	{
		Canvas->SetDrawColor(FColor(160, 190, 210, A));
		Canvas->DrawText(Small, FString::Printf(TEXT("Build %s   %s"), GX_VERSION_STRING, GX_VERSION_DATE), 80.f, H * 0.34f + 52.f, 1.2f, 1.2f);
		Canvas->SetDrawColor(FColor(220, 230, 235, A));
		Canvas->DrawText(Small, Status, 80.f, H * 0.48f, 1.35f, 1.35f);
	}

	const float BarW = FMath::Min(640.f, W - 160.f);
	const float BarH = 22.f;
	const float BarX = 80.f;
	const float BarY = H * 0.54f;
	HUD->DrawRect(FLinearColor(0.06f, 0.08f, 0.12f, OverlayAlpha), BarX, BarY, BarW, BarH);
	HUD->DrawRect(FLinearColor(0.30f, 0.78f, 0.48f, OverlayAlpha), BarX, BarY, BarW * Progress, BarH);
	if (Small)
	{
		Canvas->SetDrawColor(FColor(200, 220, 200, A));
		Canvas->DrawText(Small, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Progress * 100.f)), BarX + BarW + 16.f, BarY, 1.3f, 1.3f);
	}
	return OverlayAlpha;
}

void GXLoadScreen::DrawVersionStrip(UCanvas* Canvas, const FString& Extra)
{
	if (!Canvas || !GEngine || !GEngine->GetSmallFont())
	{
		return;
	}
	Canvas->SetDrawColor(FColor(255, 220, 80));
	const FString Line = FString::Printf(TEXT("GX %s  %s"), GX_VERSION_STRING, *Extra);
	Canvas->DrawText(GEngine->GetSmallFont(), Line, 16.f, 14.f, 1.25f, 1.25f);
}

namespace
{
	bool GSlateOverlayActive = false;
}

bool GXLoadScreen::IsSlateOverlayActive()
{
	return GSlateOverlayActive;
}

void GXLoadScreen::SetSlateOverlayActive(bool bActive)
{
	GSlateOverlayActive = bActive;
}
