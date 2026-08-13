// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelChunkProxy.h"
#include "GXMesher.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"

using namespace UE::Geometry;

AGXVoxelChunkProxy::AGXVoxelChunkProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("GXMesh"));
	SetRootComponent(Mesh);
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bEnableComplexCollision = true;
	Mesh->bDeferCollisionUpdates = false;
}

void AGXVoxelChunkProxy::InitializeChunk(const FGXChunkKey& InCoord, int32 InLOD)
{
	ChunkCoord = InCoord;
	LOD = InLOD;
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("GXChunk_%d_%d_%d_L%d"), InCoord.X, InCoord.Y, InCoord.Z, InLOD));
#endif
}

void AGXVoxelChunkProxy::ApplyMesh(const FGXMeshBuffers& Buffers, float MetersToCm, UMaterialInterface* Material, bool bCollision)
{
	if (!Mesh)
	{
		return;
	}

	if (Buffers.IsEmpty())
	{
		if (Mesh->GetMesh()->TriangleCount() == 0)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return;
	}

	FDynamicMesh3 Dyn;
	Dyn.EnableAttributes();
	if (FDynamicMeshAttributeSet* Attr = Dyn.Attributes())
	{
		Attr->EnablePrimaryColors();
		Attr->EnableMaterialID();
	}

	TArray<int32> VertIds;
	VertIds.Reserve(Buffers.Positions.Num());
	for (int32 I = 0; I < Buffers.Positions.Num(); ++I)
	{
		const FVector P = Buffers.Positions[I] * MetersToCm;
		const int32 Vid = Dyn.AppendVertex(FVector3d(P.X, P.Y, P.Z));
		VertIds.Add(Vid);
		if (FDynamicMeshNormalOverlay* Normals = Dyn.Attributes()->PrimaryNormals())
		{
			const FVector N = Buffers.Normals.IsValidIndex(I) ? Buffers.Normals[I] : FVector::UpVector;
			Normals->AppendElement(FVector3f(N.X, N.Y, N.Z));
		}
		if (FDynamicMeshColorOverlay* Colors = Dyn.Attributes()->PrimaryColors())
		{
			const FLinearColor C = Buffers.Colors.IsValidIndex(I) ? Buffers.Colors[I] : FLinearColor::White;
			Colors->AppendElement(FVector4f(C.R, C.G, C.B, C.A));
		}
	}

	for (int32 I = 0; I + 2 < Buffers.Indices.Num(); I += 3)
	{
		const int32 A = Buffers.Indices[I];
		const int32 B = Buffers.Indices[I + 1];
		const int32 C = Buffers.Indices[I + 2];
		if (!VertIds.IsValidIndex(A) || !VertIds.IsValidIndex(B) || !VertIds.IsValidIndex(C))
		{
			continue;
		}
		const int32 Tid = Dyn.AppendTriangle(VertIds[A], VertIds[B], VertIds[C]);
		if (Tid != FDynamicMesh3::InvalidID)
		{
			if (FDynamicMeshNormalOverlay* Normals = Dyn.Attributes()->PrimaryNormals())
			{
				Normals->SetTriangle(Tid, FIndex3i(A, B, C));
			}
			if (FDynamicMeshColorOverlay* Colors = Dyn.Attributes()->PrimaryColors())
			{
				Colors->SetTriangle(Tid, FIndex3i(A, B, C));
			}
		}
	}

	Mesh->GetDynamicMesh()->SetMesh(MoveTemp(Dyn));
	Mesh->NotifyMeshUpdated();

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

	if (bCollision)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->UpdateCollision(false);
	}
	else
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AGXVoxelChunkProxy::ClearMesh()
{
	if (Mesh && Mesh->GetDynamicMesh())
	{
		FDynamicMesh3 Empty;
		Mesh->GetDynamicMesh()->SetMesh(MoveTemp(Empty));
		Mesh->NotifyMeshUpdated();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
