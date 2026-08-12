// Copyright Epic Games, Inc. All Rights Reserved.
// First-person Drill + Place tools (Phase 2/4).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voxel/VoxelTypes.h"
#include "VoxelTerrainToolComponent.generated.h"

class AVoxelPlanetActor;
class UCameraComponent;

UENUM(BlueprintType)
enum class EVoxelToolMode : uint8
{
	Drill UMETA(DisplayName = "Drill"),
	Place UMETA(DisplayName = "Place")
};

/**
 * Attach to the first-person character. Raycasts from camera, previews,
 * and applies dig/place with craftsmanship modifiers.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UVoxelTerrainToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelTerrainToolComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	EVoxelToolMode Mode = EVoxelToolMode::Drill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float Reach = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float BrushRadius = 120.0f; // cm in UE units (1uu=1cm) → 1.2 m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float DigStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float PlaceStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	int32 PlaceMaterialId = 2; // RockyCliff default

	/** Craftsmanship cascade hooks — raise DigSpeedMul from quality systems later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	FVoxelToolModifiers ToolModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	bool bAutoFindPlanet = true;

	UPROPERTY(BlueprintReadOnly, Category = "Tool")
	TObjectPtr<AVoxelPlanetActor> Planet;

	UPROPERTY(BlueprintReadOnly, Category = "Tool")
	FVoxelHitResult LastHit;

	UPROPERTY(BlueprintReadOnly, Category = "Tool")
	bool bHasPreview = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	bool bDrawDebugPreview = true;

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void SetMode(EVoxelToolMode NewMode) { Mode = NewMode; }

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void CycleMode();

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void PrimaryFire(bool bPressed);

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void SecondaryFire(); // quick switch or alt place

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void CyclePlaceMaterial(int32 Direction = 1);

protected:
	bool bPrimaryHeld = false;
	float FireCooldown = 0.0f;

	void TryFindPlanet();
	bool UpdateRaycast();
	void ApplyTool(float DeltaTime);
	FVector GetTraceStart() const;
	FVector GetTraceDir() const;
};
