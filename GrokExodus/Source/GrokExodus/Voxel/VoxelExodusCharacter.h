// Copyright Epic Games, Inc. All Rights Reserved.
// First-person survivor with spherical gravity + terrain tools.

#pragma once

#include "CoreMinimal.h"
#include "GrokExodusCharacter.h"
#include "VoxelExodusCharacter.generated.h"

class UVoxelTerrainToolComponent;
class UVoxelSphericalMovement;
class UInputAction;

/**
 * Extends the template FP character with voxel tools and spherical gravity movement.
 * Camera: capsule-attached, actor yaw + relative pitch (standard FPS, not inverted).
 */
UCLASS()
class AVoxelExodusCharacter : public AGrokExodusCharacter
{
	GENERATED_BODY()

public:
	AVoxelExodusCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Gravity-relative aim + move
	virtual void DoAim(float Yaw, float Pitch) override;
	virtual void DoMove(float Right, float Forward) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	TObjectPtr<UVoxelTerrainToolComponent> TerrainTool;

	UPROPERTY(EditAnywhere, Category = "Input|Voxel")
	TObjectPtr<UInputAction> DrillAction;

	UPROPERTY(EditAnywhere, Category = "Input|Voxel")
	TObjectPtr<UInputAction> ToolModeAction;

	UPROPERTY(EditAnywhere, Category = "Input|Voxel")
	TObjectPtr<UInputAction> CycleMaterialAction;

	UPROPERTY(EditAnywhere, Category = "Input|Voxel")
	TObjectPtr<UInputAction> SavePlanetAction;

	/** Pitch relative to horizon (degrees). + = look up (sky / away from planet). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	float LookPitch = 0.0f;

	/**
	 * If true, mouse Y is inverted from the raw input sign.
	 * Enhanced Input typically already sends +Y when mouse moves up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bInvertLookPitch = false;

protected:
	void OnDrillStarted();
	void OnDrillCompleted();
	void OnToolMode();
	void OnCycleMaterial();
	void OnSavePlanet();

	UVoxelSphericalMovement* GetSphericalMovement() const;
	FVector GetPlanetUp() const;
	void ConfigureFirstPersonCamera();
	void UpdateGravityRelativeCamera();
};
