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
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|HUD")
	bool bDrawDebugStrip = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|HUD")
	float FadeSeconds = 0.9f;

	/** Keep the overlay up at least this long so a fast load is still visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|HUD")
	float MinHoldSeconds = 2.5f;

protected:
	float OverlayAlpha = 1.0f;
	float Elapsed = 0.0f;
};
