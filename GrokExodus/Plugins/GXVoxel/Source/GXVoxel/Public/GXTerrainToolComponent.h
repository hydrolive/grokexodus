// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GXVoxelWorld.h"
#include "GXTerrainToolComponent.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EGXToolMode : uint8
{
	Drill,
	Place
};

UCLASS(ClassGroup = (GX), meta = (BlueprintSpawnableComponent))
class GXVOXEL_API UGXTerrainToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGXTerrainToolComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	EGXToolMode Mode = EGXToolMode::Drill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float Reach = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	float BrushRadiusM = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	int32 PlaceMaterialId = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool")
	bool bDrawDebugPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float DigSpeedMul = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float RecoveryMul = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float WearMul = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tool")
	TObjectPtr<AGXVoxelWorld> World;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TMap<int32, float> MaterialStock;

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void CycleMode();

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void PrimaryFire(bool bPressed);

	UFUNCTION(BlueprintCallable, Category = "Tool")
	void CyclePlaceMaterial(int32 Direction = 1);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	float GetTotalStock() const;

protected:
	bool bPrimaryHeld = false;
	float FireCooldown = 0.0f;

	void TryFindWorld();
	void ApplyTool();
	FVector GetTraceStart() const;
	FVector GetTraceDir() const;
};
