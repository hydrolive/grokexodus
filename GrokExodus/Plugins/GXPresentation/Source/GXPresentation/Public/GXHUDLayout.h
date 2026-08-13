// Copyright Grok Exodus. All Rights Reserved.
// Empty HUD shell. Widgets land in later presentation PRs.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GXHUDLayout.generated.h"

UCLASS()
class GXPRESENTATION_API AGXHUDLayout : public AHUD
{
	GENERATED_BODY()

public:
	AGXHUDLayout();

	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|HUD")
	bool bDrawDebugStrip = true;
};
