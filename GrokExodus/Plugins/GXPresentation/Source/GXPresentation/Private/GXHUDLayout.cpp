// Copyright Grok Exodus. All Rights Reserved.

#include "GXHUDLayout.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

AGXHUDLayout::AGXHUDLayout()
{
}

void AGXHUDLayout::DrawHUD()
{
	Super::DrawHUD();
	if (!bDrawDebugStrip || !Canvas)
	{
		return;
	}
	if (!GEngine || !GEngine->GetSmallFont())
	{
		return;
	}
	const FString Line = TEXT("GX  |  build a ship  |  physics will not forgive you");
	Canvas->SetDrawColor(FColor(220, 230, 240));
	Canvas->DrawText(GEngine->GetSmallFont(), Line, 24.0f, 20.0f);
}
