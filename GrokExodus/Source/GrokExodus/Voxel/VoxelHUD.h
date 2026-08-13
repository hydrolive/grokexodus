// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 7 – minimal on-screen status for tools / bunker / craftsmanship.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VoxelHUD.generated.h"

UCLASS()
class AVoxelHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
