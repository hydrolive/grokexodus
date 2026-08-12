// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/VoxelTypes.h"
#include "VoxelChunkActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Render + collision proxy for one streamed chunk.
 * Uses one PMC section per material with solid-color MIDs (reliable, never black).
 */
UCLASS()
class AVoxelChunkActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelChunkActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	FVoxelChunkCoord ChunkCoord;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	int32 LOD = 0;

	void InitializeChunk(const FVoxelChunkCoord& InCoord, int32 InLOD);

	/**
	 * Build mesh. Prefer MaterialIds parallel to Positions for multi-section coloring.
	 * ParentMaterial is used as MID parent (BasicShape or project mat).
	 */
	void ApplyMeshData(
		const TArray<FVector>& Positions,
		const TArray<int32>& Indices,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FLinearColor>& Colors,
		const TArray<FProcMeshTangent>& Tangents,
		const TArray<int32>& MaterialIds,
		UMaterialInterface* ParentMaterial,
		bool bCreateCollision,
		bool bCastShadows);

	void ClearMesh();

	/** Solid color MID cache (MaterialId → instance). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> ColorMIDs;

	UMaterialInstanceDynamic* GetOrCreateColorMID(int32 MaterialId, const FLinearColor& Color, UMaterialInterface* Parent);
};
