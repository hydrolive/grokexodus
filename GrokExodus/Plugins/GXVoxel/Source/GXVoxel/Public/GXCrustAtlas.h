// Copyright Grok Exodus. All Rights Reserved.
// Tangent-plane height atlas so meshing does not re-run Earth field per voxel.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelStamps.h"

/**
 * One height + material sample per cell on the local horizon.
 * 60k Earth-field evals once per view, not 40k per chunk × thousands of chunks.
 */
class GXVOXEL_API FGXCrustAtlas : public TSharedFromThis<FGXCrustAtlas, ESPMode::ThreadSafe>
{
public:
	FVector OriginDir = FVector(1, 0, 0);
	FVector Tangent = FVector(0, 1, 0);
	FVector Bitangent = FVector(0, 0, 1);
	float PlanetRadius = 60000.0f;
	float CellM = 2.5f;
	int32 Dim = 1;
	TArray<float> Height;
	TArray<uint8> Material;
	double BuildSeconds = 0.0;

	static TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe> Build(
		const FGXPlanetStampParams& Params,
		const FVector& InOriginDir,
		float HalfExtentM,
		float InCellM);

	bool TrySample(const FVector3d& PlanetLocalM, float& OutDensity, uint8& OutMat) const;
	float SampleHeight(const FVector3f& UnitDir) const;
	bool ContainsDir(const FVector& UnitDir) const;

	bool SaveToFile(const FString& Path) const;
	static TSharedPtr<FGXCrustAtlas, ESPMode::ThreadSafe> LoadFromFile(const FString& Path);
};
