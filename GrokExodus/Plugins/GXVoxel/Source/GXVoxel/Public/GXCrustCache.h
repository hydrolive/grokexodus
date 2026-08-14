// Copyright Grok Exodus. All Rights Reserved.
// On-disk crust meshes + a worker mailbox that outlives the world actor.
#pragma once

#include "CoreMinimal.h"
#include "GXMesher.h"
#include "GXSnapshot.h"
#include "GXVoxelTypes.h"

struct FGXPlanetStampParams;

/** Worker results land here. Never capture AGXVoxelWorld* on a mesh job. */
struct FGXMeshMailbox : public TSharedFromThis<FGXMeshMailbox, ESPMode::ThreadSafe>
{
	struct FItem
	{
		FGXChunkKey Coord;
		int32 LOD = 0;
		FGXGenerationStamp Stamp;
		FGXMeshBuffers Mesh;
	};

	FCriticalSection CS;
	TArray<FItem> Pending;
	TAtomic<bool> bAlive{ true };
};

class GXVOXEL_API FGXCrustCache
{
public:
	static FString FingerprintHex(const FGXPlanetStampParams& Params);
	static FString CacheDir(const FGXPlanetStampParams& Params);
	static FString AtlasPath(const FGXPlanetStampParams& Params);
	static FString ChunkPath(const FGXPlanetStampParams& Params, const FGXChunkKey& Coord);

	static bool SaveMesh(const FString& Path, int32 LOD, const FGXMeshBuffers& Mesh);
	static bool LoadMesh(const FString& Path, int32& OutLOD, FGXMeshBuffers& OutMesh);
	static bool SaveHollow(const FString& Path);
	static bool IsHollow(const FString& Path);
	static void InvalidateChunk(const FGXPlanetStampParams& Params, const FGXChunkKey& Coord);
};
