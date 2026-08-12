// Copyright Epic Games, Inc. All Rights Reserved.
// Dense 32^3 chunk with dirty / bunker flags.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

/**
 * In-memory chunk of voxel cells.
 * Stored only when:
 *  - generated for meshing/collision, or
 *  - edited (dirty) for persistence, or
 *  - registered inside a bunker volume (always resident).
 */
class FVoxelChunk
{
public:
	static constexpr int32 Size = FVoxelConstants::ChunkSize;
	static constexpr int32 CellCount = Size * Size * Size;

	FVoxelChunkCoord Coord;
	int32 LOD = 0;

	/** True if any cell differs from pure procedural sample. */
	bool bDirty = false;

	/** True if this chunk intersects a private bunker volume (never unload). */
	bool bBunkerResident = false;

	/** True if mesh / collision needs rebuild. */
	bool bMeshDirty = true;

	/** Generation stamp for async discard. */
	uint64 Generation = 0;

	FVoxelChunk()
	{
		Cells.SetNum(CellCount);
		for (FVoxelCell& C : Cells)
		{
			C = FVoxelCell::Air();
		}
	}

	explicit FVoxelChunk(const FVoxelChunkCoord& InCoord, int32 InLOD = 0)
		: Coord(InCoord)
		, LOD(InLOD)
	{
		Cells.SetNum(CellCount);
		for (FVoxelCell& C : Cells)
		{
			C = FVoxelCell::Air();
		}
	}

	FORCEINLINE static int32 Index(int32 X, int32 Y, int32 Z)
	{
		checkSlow(X >= 0 && X < Size && Y >= 0 && Y < Size && Z >= 0 && Z < Size);
		return X + Size * (Y + Size * Z);
	}

	FORCEINLINE bool IsInBounds(int32 X, int32 Y, int32 Z) const
	{
		return (unsigned)X < (unsigned)Size && (unsigned)Y < (unsigned)Size && (unsigned)Z < (unsigned)Size;
	}

	FORCEINLINE FVoxelCell& At(int32 X, int32 Y, int32 Z)
	{
		return Cells[Index(X, Y, Z)];
	}

	FORCEINLINE const FVoxelCell& At(int32 X, int32 Y, int32 Z) const
	{
		return Cells[Index(X, Y, Z)];
	}

	FORCEINLINE FVoxelCell& AtIndex(int32 I) { return Cells[I]; }
	FORCEINLINE const FVoxelCell& AtIndex(int32 I) const { return Cells[I]; }

	const TArray<FVoxelCell>& GetCells() const { return Cells; }
	TArray<FVoxelCell>& GetCellsMutable() { return Cells; }

	void MarkDirty()
	{
		bDirty = true;
		bMeshDirty = true;
	}

	/** Byte size of dense cell payload. */
	int32 GetPayloadBytes() const
	{
		return Cells.Num() * static_cast<int32>(sizeof(FVoxelCell));
	}

private:
	TArray<FVoxelCell> Cells;
};
