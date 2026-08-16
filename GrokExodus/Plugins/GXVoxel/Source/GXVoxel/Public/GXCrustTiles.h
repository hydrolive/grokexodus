// Copyright Grok Exodus. All Rights Reserved.
// Cube-sphere crust tiles. One visible mesh per tile — no overlapping rings.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelStamps.h"
#include "ProceduralMeshComponent.h"

class AActor;
class UMaterialInterface;

struct FGXCrustTileKey
{
	int8 Face = 0;
	int32 U = 0;
	int32 V = 0;
	int32 LOD = 0;

	bool operator==(const FGXCrustTileKey& O) const
	{
		return Face == O.Face && U == O.U && V == O.V && LOD == O.LOD;
	}
	friend uint32 GetTypeHash(const FGXCrustTileKey& K)
	{
		return HashCombine(HashCombine(GetTypeHash(K.Face), GetTypeHash(K.U)),
			HashCombine(GetTypeHash(K.V), GetTypeHash(K.LOD)));
	}
};

/**
 * Unedited crust as non-overlapping tiles.
 * Digs hide a tile (PR2). This wave only streams the stamp surface.
 */
class GXVOXEL_API FGXCrustTiles
{
public:
	void Initialize(AActor* Owner);
	void Shutdown();
	void Update(
		AActor* Owner,
		const FGXSphereStamp& Stamp,
		const FVector& ViewerLocalM,
		UMaterialInterface* Material,
		int32 MaxBuildsThisTick);
	void HideTile(const FGXCrustTileKey& Key);
	bool IsReady() const { return bReady && Live.Num() >= ReadyMin; }
	int32 NumLive() const { return Live.Num(); }

	static constexpr float TileM = 64.0f;
	static constexpr float CellM = 2.0f;
	static constexpr float StreamM = 192.0f;
	static constexpr int32 ReadyMin = 8;

private:
	struct FTile
	{
		FGXCrustTileKey Key;
		TWeakObjectPtr<UProceduralMeshComponent> Comp;
		bool bHidden = false;
	};

	TMap<FGXCrustTileKey, FTile> Live;
	AActor* OwnerCached = nullptr;
	bool bReady = false;

	static int8 FaceOf(const FVector& Dir);
	static FGXCrustTileKey KeyAt(const FVector& LocalM, int32 LOD);
	static void FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB);
	void BuildTile(FTile& Tile, const FGXSphereStamp& Stamp, UMaterialInterface* Material);
};
