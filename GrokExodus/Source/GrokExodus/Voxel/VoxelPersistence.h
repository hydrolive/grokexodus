// Copyright Epic Games, Inc. All Rights Reserved.
// Dirty-chunk serialize / deserialize for private bunker permanence.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelVolume.h"

/**
 * Binary persistence format (version 1):
 *
 *   Header:
 *     uint32 Magic ("GXVX")
 *     uint32 Version
 *     uint32 Seed
 *     float  Radius, MaxRelief, VoxelSize
 *     int32  ChunkSize
 *     int32  DirtyChunkCount
 *
 *   Per dirty chunk:
 *     int32 CoordX, CoordY, CoordZ
 *     uint8  bBunkerResident
 *     uint8  pad[3]
 *     FVoxelCell[ChunkSize^3]  dense payload
 *
 * Only dirty (edited) chunks are stored. Procedural base is regenerated from seed.
 * This is the authoritative store for Private Bunker permanence.
 */
class FVoxelPersistence
{
public:
	/** Serialize dirty regions of Volume into a byte buffer. */
	static bool SaveToBuffer(const FVoxelVolume& Volume, TArray<uint8>& OutBuffer);

	/** Load dirty regions into Volume (replaces existing dirty chunks with file contents). */
	static bool LoadFromBuffer(FVoxelVolume& Volume, const TArray<uint8>& Buffer);

	/** Save to disk (absolute or project-relative path). */
	static bool SaveToFile(const FVoxelVolume& Volume, const FString& FilePath);

	/** Load from disk. */
	static bool LoadFromFile(FVoxelVolume& Volume, const FString& FilePath);

	/** Round-trip identity check helper for tests. */
	static bool BuffersEqual(const TArray<uint8>& A, const TArray<uint8>& B);
};
