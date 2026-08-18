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

	/** Drop verts that no index references. CaveMeshNear walks every vert. */
	void CompactUnusedVertices()
	{
		if (Indices.Num() < 3 || Positions.Num() == 0)
		{
			Reset();
			return;
		}
		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, Positions.Num());
		TArray<FVector> NewPos;
		TArray<FVector> NewN;
		TArray<FVector2D> NewUV;
		TArray<FLinearColor> NewC;
		TArray<int32> NewM;
		NewPos.Reserve(Positions.Num());
		auto Take = [&](int32 I) -> int32
		{
			if (!Positions.IsValidIndex(I))
			{
				return INDEX_NONE;
			}
			if (Remap[I] != INDEX_NONE)
			{
				return Remap[I];
			}
			const int32 Dst = NewPos.Num();
			Remap[I] = Dst;
			NewPos.Add(Positions[I]);
			if (Normals.IsValidIndex(I))
			{
				NewN.Add(Normals[I]);
			}
			if (UV0.IsValidIndex(I))
			{
				NewUV.Add(UV0[I]);
			}
			if (Colors.IsValidIndex(I))
			{
				NewC.Add(Colors[I]);
			}
			if (MaterialIds.IsValidIndex(I))
			{
				NewM.Add(MaterialIds[I]);
			}
			return Dst;
		};
		TArray<int32> NewI;
		NewI.Reserve(Indices.Num());
		for (int32 T = 0; T + 2 < Indices.Num(); T += 3)
		{
			const int32 A = Take(Indices[T]);
			const int32 B = Take(Indices[T + 1]);
			const int32 C = Take(Indices[T + 2]);
			if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE)
			{
				continue;
			}
			NewI.Add(A);
			NewI.Add(B);
			NewI.Add(C);
		}
		Positions = MoveTemp(NewPos);
		Indices = MoveTemp(NewI);
		Normals = MoveTemp(NewN);
		UV0 = MoveTemp(NewUV);
		Colors = MoveTemp(NewC);
		MaterialIds = MoveTemp(NewM);
		if (Indices.Num() < 3)
		{
			Reset();
		}
	}
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
