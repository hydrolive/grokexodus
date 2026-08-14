// Copyright Grok Exodus. All Rights Reserved.
// Near-field HISM scatter driven by the Earth stamp. Not a UE Landscape.
#pragma once

#include "CoreMinimal.h"

class AActor;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class FGXSphereStamp;

/**
 * Places grass / bushes / trees as HISMs on the voxel crust.
 *
 * Import a pack (Brushify Forest or any other) as *static meshes*, then drop
 * or rename them to:
 *   /Game/Foliage/SM_Grass
 *   /Game/Foliage/SM_Bush
 *   /Game/Foliage/SM_Tree
 *
 * Do not convert the planet to a Landscape actor. Voxels stay the ground;
 * foliage is decoration instanced on near chunks from biome / slope / altitude.
 */
class GXVOXEL_API FGXFoliageScatter
{
public:
	void Initialize(AActor* Owner);
	void Shutdown();
	void Sync(AActor* Owner, const FGXSphereStamp& Stamp, const FVector& ViewerWorldCm, float PlanetRadiusM);
	void Clear();
	bool HasMeshes() const { return bHasMeshes; }

private:
	struct FLayer
	{
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Comp;
		float SpacingM = 2.2f;
		float RadiusM = 56.0f;
		float ScaleMin = 0.85f;
		float ScaleMax = 1.35f;
		int32 Kind = 0; // 0 grass, 1 bush, 2 tree
	};

	TArray<FLayer> Layers;
	FVector LastViewerCm = FVector(1e12f, 0, 0);
	bool bHasMeshes = false;

	static UStaticMesh* TryLoad(const TCHAR* Path);
	static bool AllowAt(const FGXSphereStamp& Stamp, const FVector3f& Dir, int32 Kind, int32 MaterialId, float SlopeDeg, float HeightM);
};
