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
	Mesh->SetCastShadow(true);
	Mesh->bCastDynamicShadow = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->bAffectDynamicIndirectLighting = true;
	Mesh->bCastContactShadow = true;
	Mesh->SetVisibleInRayTracing(false);
	Mesh->bNeverDistanceCull = false;
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

bool AGXVoxelChunkProxy::HasCollision() const
{
	return HasRenderableMesh() && Mesh && Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
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

	// Always async. Sync-cooking the 160 m collision island while walking
	// was the 14 FPS hitch in the 0.7.11 log.
	Mesh->bUseAsyncCooking = true;
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

	// Lit vertex-color so holes and brush edits read with shadows.
	UMaterialInterface* Mat = Material;
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterial.DebugMeshMaterial"));
	}
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	}
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (Mat && Mat->GetName().Contains(TEXT("BasicShape")))
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, this))
		{
			const FLinearColor C = Colors.Num() > 0 ? Colors[0] : FLinearColor(0.38f, 0.62f, 0.28f);
			MID->SetVectorParameterValue(TEXT("BaseColor"), C);
			MID->SetVectorParameterValue(TEXT("Color"), C);
			Mat = MID;
		}
	}
	if (!Mat)
	{
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	Mesh->SetMaterial(0, Mat);
	Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(bCollision);
	Mesh->bCastDynamicShadow = bCollision;
	Mesh->bCastContactShadow = bCollision;
	Mesh->SetVisibleInRayTracing(bCollision);
}

void AGXVoxelChunkProxy::ClearMesh()
{
	if (Mesh)
	{
		Mesh->ClearAllMeshSections();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
