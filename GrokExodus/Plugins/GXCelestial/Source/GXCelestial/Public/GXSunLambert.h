// Copyright Grok Exodus. All Rights Reserved.
// Unlit Lambert so atmosphere / SkyLight cannot tint the moon or vessel red.
#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMeshComponent;

struct GXCELESTIAL_API FGXSunLambert
{
	static UMaterialInstanceDynamic* Apply(UMeshComponent* Mesh, const FLinearColor& Albedo);
	static void SetSunDir(UMaterialInstanceDynamic* MID, const FVector3d& SunBodyDir);
};
