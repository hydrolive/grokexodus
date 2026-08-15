// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GXSnapshot.h"
#include "GXVoxelTypes.h"

class FGXVoxelSnapshot;

struct FGXMeshBuffers
{
	TArray<FVector> Positions; // planet-local meters
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<int32> MaterialIds;

	void Reset()
	{
		Positions.Reset();
		Indices.Reset();
		Normals.Reset();
		UV0.Reset();
		Colors.Reset();
		MaterialIds.Reset();
	}

	bool IsEmpty() const { return Positions.Num() == 0 || Indices.Num() == 0; }
};

class GXVOXEL_API FGXMesher
{
public:
	struct FSettings
	{
		float IsoLevel = 0.0f;
		bool bComputeNormals = true;
		int32 LOD = 0;
		/** Drop open chunk-face edges toward the core so LOD0/1 T-junctions do not show sky. */
		bool bTransvoxelSkirts = true;
	};

	/** Mesh one chunk from a worker-safe snapshot. Does not allocate volume pages. */
	static FGXMeshBuffers MeshChunk(
		const FGXVoxelSnapshot& Snapshot,
		const FGXChunkKey& Coord,
		const FSettings& Settings = FSettings());
};
