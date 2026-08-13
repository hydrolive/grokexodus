// Copyright Grok Exodus. All Rights Reserved.
// HUD-independent boot overlay. Canvas AHUD is skipped in some PIE viewports.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GXBootOverlaySubsystem.generated.h"

class SWidget;
class UGameViewportClient;

/**
 * Adds a Slate widget to the game viewport on PIE/game start.
 * Draws the loading screen and a permanent GX version stamp without AHUD.
 */
UCLASS()
class GXPRESENTATION_API UGXBootOverlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	UPROPERTY(EditAnywhere, Category = "GX|Boot")
	float FadeSeconds = 0.9f;

	UPROPERTY(EditAnywhere, Category = "GX|Boot")
	float MinHoldSeconds = 2.5f;

private:
	void TryAttach();
	void RemoveOverlay();
	void WriteRunningVersionFile() const;

	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> HostViewport;
	bool bAttached = false;
	float OverlayAlpha = 1.0f;
	float Elapsed = 0.0f;
};
