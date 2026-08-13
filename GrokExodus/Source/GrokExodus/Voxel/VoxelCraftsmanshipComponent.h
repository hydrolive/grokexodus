// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 7 – material yield inventory + tool quality cascade.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voxel/VoxelTypes.h"
#include "VoxelCraftsmanshipComponent.generated.h"

/**
 * Tracks recovered dig yield and tool quality/durability.
 * Hooks into dig results without changing the density solver.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UVoxelCraftsmanshipComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelCraftsmanshipComponent();

	/** Tool quality ≥ 0.25. Higher = faster dig, less wear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float ToolQuality = 1.0f;

	/** Remaining durability [0..MaxDurability]. 0 = broken (slow dig). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float ToolDurability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float MaxDurability = 100.0f;

	/** Recovered material volume by material id (planet m³ approx). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craftsmanship")
	TMap<int32, float> MaterialStock;

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	FVoxelToolModifiers MakeModifiers() const;

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	void ApplyDigResult(const FVoxelDigResult& Result);

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	void CycleToolQuality(int32 Direction = 1);

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	void RepairTool(float Amount = 25.0f);

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	float GetStock(int32 MaterialId) const;

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	float GetTotalStock() const;

	UFUNCTION(BlueprintCallable, Category = "Craftsmanship")
	FString GetStatusLine() const;
};
