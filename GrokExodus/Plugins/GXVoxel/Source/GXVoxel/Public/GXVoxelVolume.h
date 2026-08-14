// Copyright Grok Exodus. All Rights Reserved.
// Sparse 8³ dirty pages over a procedural stamp. Meshing must not allocate.
#pragma once

#include "CoreMinimal.h"
#include "GXSnapshot.h"
#include "GXVoxelStamps.h"
#include "GXVoxelTypes.h"
#include "GXCrustAtlas.h"

struct FGXVoxelPage
{
	TStaticArray<FGXVoxelPacked, FGXVoxelConstants::CellsPerPage> Cells;
	bool bUniform = false;
	FGXVoxelPacked Uniform = FGXVoxelPacked::MakeAir();

	FORCEINLINE static int32 Index(int32 X, int32 Y, int32 Z)
	{
		return X + FGXVoxelConstants::PageSize * (Y + FGXVoxelConstants::PageSize * Z);
	}

	FGXVoxelPacked Get(int32 X, int32 Y, int32 Z) const
	{
		return bUniform ? Uniform : Cells[Index(X, Y, Z)];
	}

	void Set(int32 X, int32 Y, int32 Z, const FGXVoxelPacked& C)
	{
		if (bUniform)
		{
			for (int32 I = 0; I < FGXVoxelConstants::CellsPerPage; ++I)
			{
				Cells[I] = Uniform;
			}
			bUniform = false;
		}
		Cells[Index(X, Y, Z)] = C;
	}
};

using FGXVoxelPageRef = TSharedRef<FGXVoxelPage, ESPMode::ThreadSafe>;

/** Immutable snapshot published to mesh workers. */
class GXVOXEL_API FGXVoxelSnapshot : public FGXSnapshotBase
{
public:
	FGXPlanetStampParams Params;
	TMap<FGXChunkKey, TArray<TSharedPtr<const FGXVoxelPage, ESPMode::ThreadSafe>>> Pages;

	FGXVoxelPacked Sample(const FVector3d& PlanetLocalM) const;
	bool HasStored(const FVector3d& PlanetLocalM) const;
	bool TryGetAuthoritative(const FVector3d& PlanetLocalM, FGXVoxelPacked& Out) const;

	/** Shared height atlas. Workers read this instead of re-running the Earth stamp. */
	TSharedPtr<const FGXCrustAtlas, ESPMode::ThreadSafe> Atlas;
};

/**
 * Authoritative volume. Game thread mutates; workers read snapshots.
 * Unedited crust is never allocated.
 */
class GXVOXEL_API FGXVoxelVolume
{
public:
	explicit FGXVoxelVolume(const FGXPlanetStampParams& Params = FGXPlanetStampParams::LegacyPrototype());

	const FGXSphereStamp& GetStamp() const { return Stamp; }
	FGXGenerationStamp GetStampValue() const { return Generation; }

	FGXVoxelPacked Sample(const FVector3d& PlanetLocalM) const;
	bool TryGetAuthoritative(const FVector3d& PlanetLocalM, FGXVoxelPacked& Out) const;
	float SampleDensity(const FVector3d& PlanetLocalM) const { return Sample(PlanetLocalM).ToDensityMeters(); }

	/** Write a cell (allocates only the 8³ page). Returns new generation. */
	FGXGenerationStamp SetVoxel(const FIntVector& VoxelCoord, const FGXVoxelPacked& Cell);

	struct FBrushResult
	{
		float VolumeChanged = 0.0f;
		int32 DominantMaterialId = 0;
		TArray<FGXChunkKey> DirtyChunks;
	};

	FBrushResult ApplySphereBrush(
		const FVector3d& CenterM,
		float RadiusM,
		bool bDig,
		uint8 PlaceMaterial,
		float Strength = 1.0f);

	int32 GetAllocatedPageCount() const;
	int64 GetAllocatedBytes() const;

	void GetAllocatedChunkKeys(TArray<FGXChunkKey>& Out) const;
	bool ChunkHasEdits(const FGXChunkKey& Key) const;

	/** Copy-on-write snapshot for workers. */
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> PublishSnapshot() const;

	static FIntVector WorldToVoxel(const FVector3d& PlanetLocalM, float VoxelSize);
	static FGXChunkKey VoxelToChunk(const FIntVector& V);
	static void VoxelToPage(const FIntVector& V, FGXChunkKey& OutChunk, FGXPageKey& OutPage, FIntVector& OutLocal);

private:
	FGXSphereStamp Stamp;
	FGXGenerationStamp Generation;
	TMap<FGXChunkKey, TArray<TSharedPtr<FGXVoxelPage, ESPMode::ThreadSafe>>> Pages;
};
