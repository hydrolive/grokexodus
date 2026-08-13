// Copyright Epic Games, Inc. All Rights Reserved.
// First-person survivor with spherical gravity + terrain tools.

#pragma once

#include "CoreMinimal.h"
#include "GrokExodusCharacter.h"
#include "VoxelExodusCharacter.generated.h"

class UVoxelTerrainToolComponent;
class UVoxelCraftsmanshipComponent;
class UVoxelSphericalMovement;
class UInputAction;

/**
 * Spherical FPS survivor (Phase 4 + 7).
 * Look: world horizon vector + pitch with parallel transport.
 * Phase 7: bunker claim, craftsmanship stock, summon temporary walker.
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

	virtual void DoAim(float Yaw, float Pitch) override;
	virtual void DoMove(float Right, float Forward) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	TObjectPtr<UVoxelTerrainToolComponent> TerrainTool;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	TObjectPtr<UVoxelCraftsmanshipComponent> Craftsmanship;

	/** Horizon look direction (world, unit, perpendicular to planet up). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector LookHoriz = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	float LookPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bInvertLookPitch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float LookSensitivity = 1.0f;

	/** Bunker half-extents in cm when claiming (default ~8×8×5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunker")
	FVector BunkerHalfExtentsCm = FVector(800.f, 800.f, 500.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector2D DebugMoveInput = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector2D DebugLookInput = FVector2D::ZeroVector;

protected:
	void OnDrillStarted();
	void OnDrillCompleted();
	void OnToolMode();
	void OnCycleMaterial();
	void OnSavePlanet();
	void OnClaimBunker();
	void OnSummonWalker();
	void OnCycleToolQuality();
	void OnRepairTool();

	UVoxelSphericalMovement* GetSphericalMovement() const;
	FVector GetPlanetUp() const;
	void ConfigureFirstPersonCamera();
	void EnsureLookBasis();
	void ApplyLookAndBody();
	void SyncToolModifiers();

	float PendingMoveForward = 0.f;
	float PendingMoveRight = 0.f;
	bool bLookBasisValid = false;
};
