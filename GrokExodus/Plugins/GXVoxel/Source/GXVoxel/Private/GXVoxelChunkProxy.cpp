// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelChunkProxy.h"
#include "GXMesher.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

AGXVoxelChunkProxy::AGXVoxelChunkProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GXMesh"));
	SetRootComponent(Mesh);
	Mesh->bUseAsyncCooking = false;
	Mesh->SetCastShadow(false);
	Mesh->bCastDynamicShadow = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bUseComplexAsSimpleCollision = true;
}

void AGXVoxelChunkProxy::InitializeChunk(const FGXChunkKey& InCoord, int32 InLOD)
{
	ChunkCoord = InCoord;
	LOD = InLOD;
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("GXChunk_%d_%d_%d_L%d"), InCoord.X, InCoord.Y, InCoord.Z, InLOD));
#endif
}

bool AGXVoxelChunkProxy::HasRenderableMesh() const
{
	return Mesh && Mesh->GetNumSections() > 0;
}

void AGXVoxelChunkProxy::ApplyMesh(
	const FGXMeshBuffers& Buffers,
	const FVector& ChunkOriginMeters,
	float MetersToCm,
	UMaterialInterface* Material,
	bool bCollision)
{
	if (!Mesh)
	{
		return;
	}

	if (Buffers.IsEmpty())
	{
		return;
	}

	TArray<FVector> LocalPos;
	TArray<FVector> Normals = Buffers.Normals;
	TArray<FVector2D> UV0 = Buffers.UV0;
	TArray<FLinearColor> Colors = Buffers.Colors;
	TArray<FProcMeshTangent> Tangents;
	LocalPos.Reserve(Buffers.Positions.Num());
	Tangents.Reserve(Buffers.Positions.Num());
	for (int32 I = 0; I < Buffers.Positions.Num(); ++I)
	{
		LocalPos.Add((Buffers.Positions[I] - ChunkOriginMeters) * MetersToCm);
		FVector T = FVector::CrossProduct(
			Normals.IsValidIndex(I) ? Normals[I] : FVector::UpVector,
			FVector::UpVector);
		if (T.SizeSquared() < 1e-6f)
		{
			T = FVector::RightVector;
		}
		T.Normalize();
		Tangents.Add(FProcMeshTangent(T, false));
	}

	Mesh->bUseAsyncCooking = !bCollision;
	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(
		0,
		LocalPos,
		Buffers.Indices,
		Normals,
		UV0,
		Colors,
		Tangents,
		bCollision);

	UMaterialInterface* Mat = Material;
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Voxel/Materials/M_VoxelTerrain_VertexColor.M_VoxelTerrain_VertexColor"));
	}
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	}
	if (!Mat)
	{
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	Mesh->SetMaterial(0, Mat);
	Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AGXVoxelChunkProxy::ClearMesh()
{
	if (Mesh)
	{
		Mesh->ClearAllMeshSections();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
