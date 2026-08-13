// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GrokExodusCharacter.h"
#include "GXExodusCharacter.generated.h"

class UGXTerrainToolComponent;
class UGXVoxelInvokerComponent;
class UGXBodyMovement;

UCLASS()
class AGrokExodusSurvivor : public AGrokExodusCharacter
{
	GENERATED_BODY()

public:
	AGrokExodusSurvivor(const FObjectInitializer& ObjectInitializer);

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DoAim(float Yaw, float Pitch) override;
	virtual void DoMove(float Right, float Forward) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX")
	TObjectPtr<UGXTerrainToolComponent> TerrainTool;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX")
	TObjectPtr<UGXVoxelInvokerComponent> VoxelInvoker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector LookHoriz = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	float LookPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bInvertLookPitch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float LookSensitivity = 1.0f;

protected:
	void OnDrillStarted();
	void OnDrillCompleted();
	void OnToolMode();
	void OnCycleMaterial();
	void OnSaveWorld();
	void OnCycleQuality();

	UGXBodyMovement* GetBodyMove() const;
	FVector GetPlanetUp() const;
	void ConfigureCamera();
	void EnsureLookBasis();
	void ApplyLookAndBody();

	bool bLookBasisValid = false;
};
