// Copyright Grok Exodus. All Rights Reserved.
// Whole-planet coarse stamp mesh. Orbital LOD — not a mean-radius sphere.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "ProceduralMeshComponent.h"
#include "GXVoxelStamps.h"

class AActor;
class UProceduralMeshComponent;
class UMaterialInterface;

class GXVOXEL_API FGXPlanetGlobe
{
public:
	void Ensure(AActor* Owner, const FGXSphereStamp& Stamp, UMaterialInterface* Material);
	void Shutdown();
	bool IsReady() const { return bReady; }
	/** Drop globe tris whose stamp centroid is inside the edit island. */
	int32 PunchWhere(const TFunction<bool(const FVector&)>& Inside, UMaterialInterface* Material);

private:
	TWeakObjectPtr<UProceduralMeshComponent> Comp;
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Indices;
	TArray<int32> LiveIndices;
	bool bReady = false;
};
