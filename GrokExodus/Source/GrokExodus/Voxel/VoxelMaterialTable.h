// Copyright Epic Games, Inc. All Rights Reserved.
// Material ID → PBR set + hardness + dig yield

#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.h"
#include "VoxelMaterialTable.generated.h"

/** Built-in landscape material IDs (Phase 0/3). Extend freely. */
UENUM(BlueprintType)
enum class EVoxelMaterialId : uint8
{
	Air              = 0,
	TemperateGrass   = 1,
	RockyCliff       = 2,
	DryDirt          = 3,
	SandCoastal      = 4,
	SnowIce          = 5,
	WetMud           = 6,
	VolcanicScorched = 7,
	BedrockDeep      = 8,
	// Reserve 9–255 for ores, concrete, bunker liners, etc.
	Count            UMETA(Hidden)
};

/**
 * Authoritative material definition.
 * Texture paths are soft references; loaded by the render layer (Phase 3).
 */
USTRUCT(BlueprintType)
struct FVoxelMaterialDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	int32 Id = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FName Name = NAME_None;

	/** Dig resistance. Higher = slower dig. Craftsmanship multiplies effective rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float Hardness = 1.0f;

	/** Fraction of removed volume returned as resources [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float DigYield = 0.75f;

	/** Base tool wear per unit volume removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float WearFactor = 1.0f;

	/** Default density written when placing this material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float PlaceDensity = 1.0f;

	// --- PBR soft paths (Content relative, populated Phase 3) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath Albedo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath Metallic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath Roughness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FSoftObjectPath AmbientOcclusion;

	/** Debug / fallback solid color when textures missing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR")
	FLinearColor DebugColor = FLinearColor::Gray;
};

/**
 * Runtime material lookup table.
 * Not a UObject so pure data harnesses can use it without the engine world.
 */
class FVoxelMaterialTable
{
public:
	FVoxelMaterialTable();

	void ResetToDefaults();

	const FVoxelMaterialDef* Find(int32 MaterialId) const;
	FVoxelMaterialDef& FindOrAdd(int32 MaterialId);

	float GetHardness(int32 MaterialId) const;
	float GetDigYield(int32 MaterialId) const;
	float GetWearFactor(int32 MaterialId) const;
	FLinearColor GetDebugColor(int32 MaterialId) const;

	const TMap<int32, FVoxelMaterialDef>& GetAll() const { return Materials; }

	/** Effective dig rate: base / hardness * tool.DigSpeedMul (never zero). */
	static float ComputeDigRate(float Hardness, const FVoxelToolModifiers& Tool);

	/** Yield amount from removed volume. */
	static float ComputeYield(float VolumeRemoved, float DigYield, const FVoxelToolModifiers& Tool);

	/** Tool wear from volume and material. */
	static float ComputeWear(float VolumeRemoved, float WearFactor, const FVoxelToolModifiers& Tool);

private:
	TMap<int32, FVoxelMaterialDef> Materials;

	void Register(const FVoxelMaterialDef& Def);
};
