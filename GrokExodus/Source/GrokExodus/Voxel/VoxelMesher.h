// Copyright Epic Games, Inc. All Rights Reserved.
// Marching Cubes mesher — watertight crust for diggable planetary voxels.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelChunk.h"
#include "Voxel/VoxelVolume.h"
#include "Voxel/VoxelMaterialTable.h"

/** CPU mesh buffers ready for UProceduralMeshComponent. */
struct FVoxelMeshData
{
	TArray<FVector> Positions; // planet-local meters
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> MaterialIds;

	void Reset()
	{
		Positions.Reset();
		Indices.Reset();
		Normals.Reset();
		UV0.Reset();
		Colors.Reset();
		Tangents.Reset();
		MaterialIds.Reset();
	}

	bool IsEmpty() const { return Positions.Num() == 0 || Indices.Num() == 0; }
};

/**
 * Marching Cubes on a density field (positive = solid).
 * Preferred over Surface Nets here for fewer holes / better closed crust.
 * Dual Contouring remains a future upgrade for sharp dig features.
 */
class FVoxelMesher
{
public:
	struct FSettings
	{
		float IsoLevel = 0.0f;
		bool bGenerateCollision = true;
		bool bComputeNormals = true;
		bool bVertexColorsFromMaterial = true;
		int32 LOD = 0;
	};

	static FVoxelMeshData MeshChunk(
		const FVoxelVolume& Volume,
		const FVoxelChunkCoord& Coord,
		const FSettings& Settings = FSettings());

	/**
	 * @param Pad  Border samples on each side used only for continuous density;
	 *             only the interior (Size - 2*Pad) cells are meshed so neighbor
	 *             chunks meet without double-shells or LOD cracks.
	 */
	static FVoxelMeshData MeshDensityGrid(
		const TArray<float>& Densities,
		const TArray<int32>& Materials,
		int32 SizeX, int32 SizeY, int32 SizeZ,
		const FVector& Origin,
		float VoxelSize,
		const FVoxelMaterialTable& MaterialsTable,
		const FSettings& Settings = FSettings(),
		int32 Pad = 0);
};
