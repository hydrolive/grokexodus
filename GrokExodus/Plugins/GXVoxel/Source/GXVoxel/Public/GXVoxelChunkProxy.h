// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GXVoxelTypes.h"
#include "GXVoxelChunkProxy.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
struct FGXMeshBuffers;

UCLASS()
class GXVOXEL_API AGXVoxelChunkProxy : public AActor
{
	GENERATED_BODY()

public:
	AGXVoxelChunkProxy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX")
	TObjectPtr<UDynamicMeshComponent> Mesh;

	FGXChunkKey ChunkCoord;
	int32 LOD = 0;

	void InitializeChunk(const FGXChunkKey& InCoord, int32 InLOD);
	void ApplyMesh(const FGXMeshBuffers& Buffers, float MetersToCm, UMaterialInterface* Material, bool bCollision);
	void ClearMesh();
};
