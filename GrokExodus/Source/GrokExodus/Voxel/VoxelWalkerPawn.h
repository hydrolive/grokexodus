// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 7 – temporary walker. Losing it must not erase planet/bunker data.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VoxelWalkerPawn.generated.h"

class UCameraComponent;
class UVoxelSphericalMovement;
class AVoxelPlanetActor;

/**
 * Disposable surface vehicle. Spherical gravity + foot probes.
 * Does not write the voxel volume. Destroy freely — bunkers persist.
 */
UCLASS()
class AVoxelWalkerPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AVoxelWalkerPawn(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walker")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	float DriveSpeed = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	float TurnRateDeg = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	float FootProbeRadiusCm = 40.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Walker")
	bool bFeetOnTerrain = false;

	UPROPERTY(BlueprintReadOnly, Category = "Walker")
	TObjectPtr<AVoxelPlanetActor> Planet;

	/** Cargo bay — temporary. Destroyed with walker (bunkers/stock on planet stay). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walker|Cargo")
	TMap<int32, float> CargoStock;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker|Cargo")
	float MaxCargoVolume = 50.0f;

	UFUNCTION(BlueprintCallable, Category = "Walker")
	void EjectDriver();

	UFUNCTION(BlueprintCallable, Category = "Walker")
	void SelfDestruct();

	UFUNCTION(BlueprintCallable, Category = "Walker|Cargo")
	float GetCargoTotal() const;

	UFUNCTION(BlueprintCallable, Category = "Walker|Cargo")
	void LoadCargoFrom(TMap<int32, float>& SourceStock);

	UFUNCTION(BlueprintCallable, Category = "Walker|Cargo")
	void UnloadCargoTo(TMap<int32, float>& DestStock);

	UFUNCTION(BlueprintCallable, Category = "Walker|Cargo")
	FString GetCargoStatusLine() const;

protected:
	void MoveForward(float Value);
	void TurnRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void UpdateFootProbes();
	void TryFindPlanet();
	UVoxelSphericalMovement* GetSphericalMove() const;

	float LookYawAccum = 0.f;
	float LookPitchAccum = 0.f;
	float Throttle = 0.f;
	float Steer = 0.f;
};
