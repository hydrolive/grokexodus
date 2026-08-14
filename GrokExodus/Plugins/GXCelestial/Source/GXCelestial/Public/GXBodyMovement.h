// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GXBodyMovement.generated.h"

/**
 * Inverse-square gravity toward the active IGXGravityField.
 * The planet actor never rotates; this only aims the capsule.
 */
UCLASS(ClassGroup = (GX), meta = (BlueprintSpawnableComponent))
class GXCELESTIAL_API UGXBodyMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGXBodyMovement();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	bool bAutoFindField = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	bool bAlignCapsuleToGravity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	float AlignSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	bool bUnstickFromSolid = true;

	/** Last-resort snap if we truly leave the crust (not the walk floor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	bool bSnapWhenAirborne = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Gravity")
	float AirborneSnapSeconds = 0.40f;

	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult = nullptr) const override;

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	FVector GetGravityDir() const;

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	FVector GetUpDir() const { return -GetGravityDir(); }

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	void SnapToSurface(bool bZeroVelocity = true);

	/** Skip airborne crust-snap for this many seconds (call when the player jumps). */
	void NotifyPlayerJumped();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void TryFindField();

protected:
	UPROPERTY()
	TObjectPtr<AActor> FieldActor;

	void UpdateGravity();
	void AlignCapsule(float DeltaSeconds);
	void UnstickIfBuried(float DeltaSeconds);
	bool HasSolidWithinMeters(float MaxMeters) const;
	bool FindStampSurface(const FVector& CapsuleLocation, FVector& OutSurfaceCm, FVector& OutCapsuleCm) const;
	void StickToStampFloor();
	bool IsJumpingUp() const;

	float AirborneSeconds = 0.0f;
	float JumpIgnoreSnapSeconds = 0.0f;
};
