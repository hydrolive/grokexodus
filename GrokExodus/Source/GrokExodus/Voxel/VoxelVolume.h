// Copyright Epic Games, Inc. All Rights Reserved.
// Sparse chunk hash volume — authoritative editable store.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelChunk.h"
#include "Voxel/VoxelSphereMapping.h"
#include "Voxel/VoxelMaterialTable.h"

/**
 * Sparse hierarchical-ready volume.
 *
 * Storage model:
 *   - Procedural FVoxelSphereMapping is the base layer (infinite, free).
 *   - TMap<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>> holds only allocated chunks.
 *   - Edited regions set bDirty and override procedural samples.
 *   - Bunker-resident chunks never unload.
 *
 * Threading notes (Phase 5):
 *   - Read queries may run on worker threads if chunk map is snapshotted.
 *   - Mutations (Phase 2+) must be game-thread or mutex-guarded.
 */
class FVoxelVolume
{
public:
	explicit FVoxelVolume(const FVoxelPlanetParams& Params = FVoxelPlanetParams());

	const FVoxelSphereMapping& GetMapping() const { return Mapping; }
	FVoxelSphereMapping& GetMappingMutable() { return Mapping; }
	const FVoxelMaterialTable& GetMaterials() const { return Materials; }
	FVoxelMaterialTable& GetMaterialsMutable() { return Materials; }

	const FVoxelPlanetParams& GetPlanetParams() const { return Mapping.GetParams(); }

	// ---- Queries ----

	/** Sample density: dirty chunk override if present, else procedural. */
	float SampleDensity(const FVector& PlanetLocalPos) const;

	/** Sample full cell. */
	FVoxelCell SampleCell(const FVector& PlanetLocalPos) const;

	/** Sample by global LOD0 voxel coordinate. */
	FVoxelCell SampleVoxel(const FIntVector& VoxelCoord) const;

	// ---- Chunk management ----

	/** Get existing chunk or nullptr. */
	FVoxelChunk* FindChunk(const FVoxelChunkCoord& Coord);
	const FVoxelChunk* FindChunk(const FVoxelChunkCoord& Coord) const;

	/** Get or allocate chunk filled from procedural samples. */
	FVoxelChunk& GetOrCreateChunk(const FVoxelChunkCoord& Coord);

	/** Fill chunk cells from procedural mapping (does not clear bDirty if already dirty). */
	void FillChunkProcedural(FVoxelChunk& Chunk) const;

	/** Unload non-dirty, non-bunker chunks outside Keep set (streaming). */
	int32 UnloadUnusedChunks(const TSet<FVoxelChunkCoord>& Keep);

	/** Number of allocated chunks. */
	int32 GetAllocatedChunkCount() const { return Chunks.Num(); }

	/** Approximate memory of allocated cell payloads. */
	int64 GetAllocatedMemoryBytes() const;

	const TMap<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>>& GetChunks() const { return Chunks; }

	/** Collect all dirty chunk coords. */
	void GetDirtyChunkCoords(TArray<FVoxelChunkCoord>& Out) const;

	// ---- Mutation (Phase 2 uses these; available for harness in Phase 0) ----

	/**
	 * Write a single voxel (allocates chunk, marks dirty).
	 * Neighbor chunks are NOT auto-marked; caller should remesh neighbors if surface crosses.
	 */
	void SetVoxel(const FIntVector& VoxelCoord, const FVoxelCell& Cell);

	/**
	 * Spherical brush edit: Remove (dig) or Add (place).
	 * Returns approximate volume of solid changed and dirty chunk list.
	 */
	struct FBrushResult
	{
		float VolumeChanged = 0.0f;
		int32 DominantMaterialId = 0;
		TArray<FVoxelChunkCoord> DirtyChunks;
	};

	FBrushResult ApplySphereBrush(
		const FVector& Center,
		float Radius,
		bool bDig,
		int32 PlaceMaterialId,
		const FVoxelToolModifiers& Tool,
		float Strength = 1.0f);

	// ---- Bunker hooks ----

	/** Mark all chunks overlapping AABB as bunker-resident (permanent safe anchor). */
	void RegisterBunkerVolume(const FBox& PlanetLocalBounds);

	void ClearBunkerFlags();

private:
	FVoxelSphereMapping Mapping;
	FVoxelMaterialTable Materials;
	TMap<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>> Chunks;

	void MarkChunkAndNeighborsDirty(const FVoxelChunkCoord& Coord, TSet<FVoxelChunkCoord>& DirtySet);
};
