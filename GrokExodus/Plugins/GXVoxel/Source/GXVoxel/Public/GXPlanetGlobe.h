// Copyright Grok Exodus. All Rights Reserved.
// Whole-planet coarse stamp mesh. Orbital LOD — not a mean-radius sphere.
#pragma once

#include "CoreMinimal.h"
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

private:
	TWeakObjectPtr<UProceduralMeshComponent> Comp;
	bool bReady = false;
};
