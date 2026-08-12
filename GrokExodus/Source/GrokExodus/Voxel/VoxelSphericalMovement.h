// Copyright Epic Games, Inc. All Rights Reserved.
// First-person movement with gravity toward planet center.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VoxelSphericalMovement.generated.h"

class AVoxelPlanetActor;

/**
 * Uses UE5 custom GravityDirection so floors, falling, and walking
 * all pull toward the planet center (not world -Z).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UVoxelSphericalMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVoxelSphericalMovement();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	TObjectPtr<AVoxelPlanetActor> Planet;

	/** World gravity acceleration magnitude (cm/s^2). Default matches UE ~980. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	float GravityStrength = 980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	bool bAutoFindPlanet = true;

	/** Rotate capsule so feet point at planet center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	bool bAlignCapsuleToGravity = true;

	/** How fast the capsule rotates to match gravity (deg-ish slerp rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	float AlignSpeed = 10.0f;

	/** If density says we are buried, push out along radial up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Gravity")
	bool bUnstickFromSolid = true;

	UFUNCTION(BlueprintCallable, Category = "Voxel|Gravity")
	FVector GetSphericalGravityDir() const;

	UFUNCTION(BlueprintCallable, Category = "Voxel|Gravity")
	FVector GetSphericalUpDir() const { return -GetSphericalGravityDir(); }

	/** Call after teleporting to surface so velocity/mode reset. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Gravity")
	void SnapToPlanetSurface(bool bZeroVelocity = true);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void TryFindPlanet();

protected:
	void UpdateGravityDirection();
	void AlignCapsuleToGravity(float DeltaSeconds);
	void UnstickIfBuried(float DeltaSeconds);
};
