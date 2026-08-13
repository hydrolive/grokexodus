// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GXVoxelTypes.h"
#include "GXVoxelChunkProxy.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
struct FGXMeshBuffers;

UCLASS()
class GXVOXEL_API AGXVoxelChunkProxy : public AActor
{
	GENERATED_BODY()

public:
	AGXVoxelChunkProxy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	FGXChunkKey ChunkCoord;
	int32 LOD = 0;

	void InitializeChunk(const FGXChunkKey& InCoord, int32 InLOD);
	void ApplyMesh(const FGXMeshBuffers& Buffers, const FVector& ChunkOriginMeters, float MetersToCm, UMaterialInterface* Material, bool bCollision);
	void ClearMesh();
	bool HasRenderableMesh() const;
	bool HasCollision() const;
};
