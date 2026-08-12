// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelChunkActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

AVoxelChunkActor::AVoxelChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("VoxelMesh"));
	SetRootComponent(Mesh);
	// Async cooking leaves a window with no collision → fall-through. LOD0 uses sync.
	Mesh->bUseAsyncCooking = false;
	Mesh->SetCastShadow(false);
	Mesh->bCastDynamicShadow = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bUseComplexAsSimpleCollision = true;
}

void AVoxelChunkActor::InitializeChunk(const FVoxelChunkCoord& InCoord, int32 InLOD)
{
	ChunkCoord = InCoord;
	LOD = InLOD;
	SetActorLabel(FString::Printf(TEXT("VoxelChunk_%d_%d_%d_L%d"), InCoord.X, InCoord.Y, InCoord.Z, InLOD));
}

UMaterialInstanceDynamic* AVoxelChunkActor::GetOrCreateColorMID(int32 MaterialId, const FLinearColor& Color, UMaterialInterface* Parent)
{
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = ColorMIDs.Find(MaterialId))
	{
		if (UMaterialInstanceDynamic* Existing = Found->Get())
		{
			Existing->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Existing->SetVectorParameterValue(TEXT("Color"), Color);
			return Existing;
		}
	}

	UMaterialInterface* ParentMat = Parent;
	if (!ParentMat)
	{
		ParentMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!ParentMat)
	{
		ParentMat = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ParentMat, this);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("DiffuseColor"), Color);
		ColorMIDs.Add(MaterialId, MID);
	}
	return MID;
}

void AVoxelChunkActor::ApplyMeshData(
	const TArray<FVector>& Positions,
	const TArray<int32>& Indices,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FLinearColor>& Colors,
	const TArray<FProcMeshTangent>& Tangents,
	const TArray<int32>& MaterialIds,
	UMaterialInterface* ParentMaterial,
	bool bCreateCollision,
	bool bCastShadows)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->ClearAllMeshSections();
	Mesh->SetCastShadow(bCastShadows);
	Mesh->bCastDynamicShadow = bCastShadows;
	// Collision-critical chunks cook synchronously so the player can stand immediately
	Mesh->bUseAsyncCooking = !bCreateCollision;

	if (Positions.Num() == 0 || Indices.Num() == 0)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	const int32 VertCount = Positions.Num();

	UMaterialInterface* Parent = ParentMaterial;
	if (!Parent)
	{
		Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!Parent)
	{
		Parent = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const bool bHasMat = (MaterialIds.Num() == VertCount);
	const bool bHasCol = (Colors.Num() == VertCount);

	auto MatOf = [&](int32 VI) -> int32
	{
		if (bHasMat && MaterialIds.IsValidIndex(VI))
		{
			return FMath::Max(1, MaterialIds[VI]);
		}
		return 1;
	};

	auto ColorOf = [&](int32 MatId, int32 VI) -> FLinearColor
	{
		if (bHasCol && Colors.IsValidIndex(VI))
		{
			FLinearColor C = Colors[VI];
			if (C.R + C.G + C.B < 0.2f)
			{
				C = FLinearColor(0.45f, 0.55f, 0.35f, 1.f);
			}
			C.A = 1.f;
			return C;
		}
		switch (MatId)
		{
		case 2: return FLinearColor(0.62f, 0.58f, 0.52f);
		case 3: return FLinearColor(0.68f, 0.48f, 0.28f);
		case 4: return FLinearColor(0.88f, 0.78f, 0.52f);
		case 5: return FLinearColor(0.95f, 0.97f, 1.0f);
		case 6: return FLinearColor(0.42f, 0.34f, 0.22f);
		case 7: return FLinearColor(0.38f, 0.30f, 0.26f);
		case 8: return FLinearColor(0.42f, 0.42f, 0.46f);
		default: return FLinearColor(0.38f, 0.62f, 0.28f);
		}
	};

	// Group tris by material
	TMap<int32, TArray<int32>> SectionIndices;
	TMap<int32, FLinearColor> MatColor;
	TArray<int32> AllIndices;
	AllIndices.Reserve(Indices.Num());

	for (int32 T = 0; T + 2 < Indices.Num(); T += 3)
	{
		const int32 I0 = Indices[T];
		const int32 I1 = Indices[T + 1];
		const int32 I2 = Indices[T + 2];
		if (!Positions.IsValidIndex(I0) || !Positions.IsValidIndex(I1) || !Positions.IsValidIndex(I2))
		{
			continue;
		}
		AllIndices.Add(I0);
		AllIndices.Add(I1);
		AllIndices.Add(I2);

		const int32 M0 = MatOf(I0);
		const int32 M1 = MatOf(I1);
		const int32 M2 = MatOf(I2);
		int32 MatId = M0;
		if (M1 == M2) MatId = M1;
		else if (M0 == M2) MatId = M0;

		SectionIndices.FindOrAdd(MatId).Append({ I0, I1, I2 });
		if (!MatColor.Contains(MatId))
		{
			MatColor.Add(MatId, ColorOf(MatId, I0));
		}
	}

	if (AllIndices.Num() < 3)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	TArray<FVector> SafeNormals = Normals;
	if (SafeNormals.Num() != VertCount)
	{
		SafeNormals.SetNum(VertCount);
		for (int32 I = 0; I < VertCount; ++I)
		{
			const FVector R = Positions[I].GetSafeNormal();
			SafeNormals[I] = R.IsNearlyZero() ? FVector::UpVector : R;
		}
	}

	TArray<FVector2D> SafeUV;
	SafeUV.SetNum(VertCount);
	TArray<FLinearColor> SafeColors;
	SafeColors.SetNum(VertCount);
	for (int32 I = 0; I < VertCount; ++I)
	{
		const int32 M = MatOf(I);
		SafeColors[I] = ColorOf(M, I);
		SafeUV[I] = FVector2D(static_cast<float>(M), 0.f);
	}
	TArray<FProcMeshTangent> SafeTangents;
	SafeTangents.SetNum(VertCount);

	// --- Section 0: FULL mesh + collision (prevents fall-through) ---
	// Visual may be multi-material; collision always uses complete index buffer.
	const int32 DominantMat = [&]()
	{
		int32 Best = 1;
		int32 BestN = 0;
		for (const auto& P : SectionIndices)
		{
			if (P.Value.Num() > BestN)
			{
				BestN = P.Value.Num();
				Best = P.Key;
			}
		}
		return Best;
	}();

	// ONE full-geometry section with collision = continuous walkable surface (no material gaps).
	// Vertex colors carry multi-material tint when VertexColorViewMode material is available.
	const FLinearColor DomCol = MatColor.Contains(DominantMat)
		? MatColor.FindRef(DominantMat)
		: ColorOf(DominantMat, 0);
	UMaterialInstanceDynamic* DomMID = GetOrCreateColorMID(DominantMat, DomCol, Parent);

	UMaterialInterface* VCMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	UMaterialInterface* DrawMat = VCMat ? VCMat : (DomMID ? static_cast<UMaterialInterface*>(DomMID) : Parent);

	Mesh->CreateMeshSection_LinearColor(
		0,
		Positions,
		AllIndices,
		SafeNormals,
		SafeUV,
		SafeColors,
		SafeTangents,
		bCreateCollision);
	Mesh->SetMaterial(0, DrawMat);

	// If no vertex-color material, add solid-color sections for other materials (visual only)
	if (!VCMat)
	{
		int32 Section = 1;
		for (TPair<int32, TArray<int32>>& Pair : SectionIndices)
		{
			if (Pair.Key == DominantMat || Pair.Value.Num() < 3)
			{
				continue;
			}
			const FLinearColor Col = MatColor.FindRef(Pair.Key);
			UMaterialInstanceDynamic* MID = GetOrCreateColorMID(Pair.Key, Col, Parent);
			Mesh->CreateMeshSection_LinearColor(
				Section,
				Positions,
				Pair.Value,
				SafeNormals,
				SafeUV,
				SafeColors,
				SafeTangents,
				false);
			Mesh->SetMaterial(Section, MID ? static_cast<UMaterialInterface*>(MID) : Parent);
			++Section;
		}
	}

	Mesh->SetCollisionEnabled(bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->MarkRenderStateDirty();
}

void AVoxelChunkActor::ClearMesh()
{
	if (Mesh)
	{
		Mesh->ClearAllMeshSections();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
