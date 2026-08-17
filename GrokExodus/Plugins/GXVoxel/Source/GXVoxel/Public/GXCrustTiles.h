// Copyright Grok Exodus. All Rights Reserved.
// Cube-sphere crust tiles. One visible mesh per tile — no overlapping rings.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelStamps.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

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
 * Dig drops verts radially with a wide smoothstep. Shared tile edges are
 * welded (min R) so a FineCell neighbor cannot rip a seam. Never delete tris.
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
		int32 MaxBuildsThisTick,
		TFunction<float(const FVector&)> DensityAt = nullptr);
	void HideTile(const FGXCrustTileKey& Key);
	int32 HideTilesInSphere(const FVector& LocalM, float RadiusM);
	int32 NotifyBrush(
		const FVector& LocalM,
		float RadiusM,
		bool bRemove,
		const FGXSphereStamp& Stamp,
		UMaterialInterface* Material,
		const TFunction<float(const FVector&)>& DensityAt,
		int32* OutPunched = nullptr);
	bool HasTileAt(const FVector& LocalM) const;
	/** Live tile verts inside RadiusM of LocalM (planet-local metres). */
	void CollectLivePointsNear(const FVector& LocalM, float RadiusM, TArray<FVector>& Out) const;
	/** True when the (2*Half+1)^2 block around the pawn is live. */
	bool HasNeighborhood(const FVector& LocalM, int32 Half) const;
	bool IsReady() const { return bReady; }
	int32 NumLive() const { return Live.Num(); }

	static constexpr float TileM = 64.0f;
	static constexpr float CellM = 1.0f;
	/** First sculpt rebuilds this tile at 0.5 m. 0.35 m was 34 k verts and missed strokes. */
	static constexpr float FineCellM = 0.50f;
	static constexpr float StreamM = 256.0f;
	static constexpr int32 ReadyMin = 9;

private:
	struct FTile
	{
		FGXCrustTileKey Key;
		TWeakObjectPtr<UProceduralMeshComponent> Comp;
		TWeakObjectPtr<UStaticMeshComponent> NaniteComp;
		TStrongObjectPtr<UStaticMesh> NaniteMesh;
		FVector OriginCm = FVector::ZeroVector;
		TArray<FVector> LivePos;
		TArray<FVector> StampDir;
		TArray<float> StampSurfM;
		TArray<FVector> LiveN;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Indices;
		float FineCell = 0.0f;
		bool bHidden = false;
		bool bSculpted = false;
	};

	TMap<FGXCrustTileKey, FTile> Live;
	TSet<FGXCrustTileKey> HiddenKeys;
	AActor* OwnerCached = nullptr;
	bool bReady = false;
	double LastNaniteCookSeconds = -1.0e9;
	double ReadyAtSeconds = -1.0e9;

	static int8 FaceOf(const FVector& Dir);
	static FGXCrustTileKey KeyAt(const FVector& LocalM, int32 LOD);
	static void FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB);
	void BuildTile(FTile& Tile, const FGXSphereStamp& Stamp, UMaterialInterface* Material,
		const TFunction<float(const FVector&)>& DensityAt);
	static int32 GridDim(const FTile& Tile);
	void WeldSeamsNear(const FVector& LocalM, float CoverM, UMaterialInterface* Material, int32& InOutChanged);
	static int32 WeldSharedU(FTile& Left, FTile& Right);
	static int32 WeldSharedV(FTile& Lo, FTile& Hi);
	static void PushTileMesh(FTile& Tile);
	static void RecomputeNormals(FTile& Tile);
	void ApplyNaniteVisual(FTile& Tile, UMaterialInterface* Material);
	void DropNanite(FTile& Tile);
	void DestroyTileVisuals(FTile& Tile);
};
