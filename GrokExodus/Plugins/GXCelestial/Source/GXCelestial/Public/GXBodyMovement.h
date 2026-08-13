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

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	FVector GetGravityDir() const;

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	FVector GetUpDir() const { return -GetGravityDir(); }

	UFUNCTION(BlueprintCallable, Category = "GX|Gravity")
	void SnapToSurface(bool bZeroVelocity = true);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void TryFindField();

protected:
	UPROPERTY()
	TObjectPtr<AActor> FieldActor;

	void UpdateGravity();
	void AlignCapsule(float DeltaSeconds);
	void UnstickIfBuried(float DeltaSeconds);
};
