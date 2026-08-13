// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GXVoxelInvokerComponent.generated.h"

/** Attach to pawns, cameras, and ships. The voxel world streams around invokers. */
UCLASS(ClassGroup = (GX), meta = (BlueprintSpawnableComponent))
class GXVOXEL_API UGXVoxelInvokerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGXVoxelInvokerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Stream")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Stream")
	float CollisionRadiusM = 64.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Stream")
	float MeshRadiusM = 200.0f;

	UFUNCTION(BlueprintPure, Category = "GX|Stream")
	FVector GetInvokerWorldLocation() const;
};
