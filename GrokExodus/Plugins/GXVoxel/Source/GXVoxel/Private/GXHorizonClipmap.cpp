// Copyright Grok Exodus. All Rights Reserved.

#include "GXHorizonClipmap.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "HAL/PlatformTime.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"

void FGXHorizonClipmap::Initialize(AActor* Owner)
{
	Shutdown();
	if (!Owner)
	{
		return;
	}

	struct FSpec { float Inner; float Outer; float Cell; };
	const FSpec Specs[] = {
		{ 160.0f, 1152.0f, 12.0f },
		{ 1080.0f, 3240.0f, 36.0f },
		{ 3168.0f, 8000.0f, 72.0f },
	};
	for (const FSpec& S : Specs)
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
		if (!PMC)
		{
			continue;
		}
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->SetCastShadow(false);
		PMC->SetVisibleInRayTracing(false);
		PMC->bUseAsyncCooking = true;
		PMC->SetupAttachment(Owner->GetRootComponent());
		PMC->RegisterComponent();
		FRing Ring;
		Ring.Comp = PMC;
		Ring.InnerM = S.Inner;
		Ring.OuterM = S.Outer;
		Ring.CellM = S.Cell;
		Rings.Add(Ring);
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap: %d rings"), Rings.Num());
}

void FGXHorizonClipmap::Shutdown()
{
	for (FRing& R : Rings)
	{
		if (UProceduralMeshComponent* C = R.Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	Rings.Reset();
	bReady = false;
}

void FGXHorizonClipmap::BuildRing(
	UProceduralMeshComponent* Comp,
	const FGXSphereStamp& Stamp,
	const FVector& CenterDir,
	const FVector& Tangent,
	const FVector& Bitangent,
	float InnerM,
	float OuterM,
	float CellM,
	UMaterialInterface* Material)
{
	if (!Comp)
	{
		return;
	}

	const int32 Half = FMath::Max(8, FMath::CeilToInt(OuterM / CellM));
	const int32 Dim = Half * 2 + 1;
	const float R0 = Stamp.GetParams().Radius;
	const float InnerPad = InnerM * InnerM;
	const float OuterPad = OuterM * OuterM;

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<int32> Indices;
	TArray<FProcMeshTangent> Tangents;
	const int32 VertGuess = Dim * Dim;
	Positions.Reserve(VertGuess);
	Normals.Reserve(VertGuess);
	UV0.Reserve(VertGuess);
	Colors.Reserve(VertGuess);
	Tangents.Reserve(VertGuess);

	TArray<int32> IndexOf;
	IndexOf.Init(INDEX_NONE, Dim * Dim);

	for (int32 J = 0; J < Dim; ++J)
	{
		const float V = static_cast<float>(J - Half) * CellM;
		for (int32 I = 0; I < Dim; ++I)
		{
			const float U = static_cast<float>(I - Half) * CellM;
			const float D2 = U * U + V * V;
			if (D2 > OuterPad * 1.21f)
			{
				continue;
			}
			FVector Dir = (CenterDir * R0 + Tangent * U + Bitangent * V).GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = CenterDir;
			}
			const FVector3f Df(Dir.X, Dir.Y, Dir.Z);
			const float SurfR = Stamp.SampleSurfaceRadius(Df);
			// Sit 1.5 m under the voxel isosurface so L0 wins depth and a
			// missing chunk still shows crust instead of a black pit.
			const FVector P = Dir * (SurfR - 1.5f) * 100.0f;
			const int32 Idx = I + J * Dim;
			IndexOf[Idx] = Positions.Num();
			Positions.Add(P);
			Normals.Add(Dir);
			UV0.Add(FVector2D(1.0f, 0.0f));
			Colors.Add(FLinearColor(0.38f, 0.48f, 0.28f, 1.0f));
			FVector T = FVector::CrossProduct(Dir, FVector::ZAxisVector);
			if (T.SizeSquared() < 1e-6f)
			{
				T = FVector::CrossProduct(Dir, FVector::YAxisVector);
			}
			T.Normalize();
			Tangents.Add(FProcMeshTangent(T, false));
		}
	}

	auto Vert = [&](int32 I, int32 J) -> int32
	{
		if (I < 0 || J < 0 || I >= Dim || J >= Dim)
		{
			return INDEX_NONE;
		}
		return IndexOf[I + J * Dim];
	};

	for (int32 J = 0; J < Dim - 1; ++J)
	{
		for (int32 I = 0; I < Dim - 1; ++I)
		{
			const int32 A = Vert(I, J);
			const int32 B = Vert(I + 1, J);
			const int32 C = Vert(I, J + 1);
			const int32 D = Vert(I + 1, J + 1);
			if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
			{
				continue;
			}
			const float CU = (static_cast<float>(I - Half) + 0.5f) * CellM;
			const float CV = (static_cast<float>(J - Half) + 0.5f) * CellM;
			const float CD2 = CU * CU + CV * CV;
			if (CD2 < InnerPad || CD2 > OuterPad)
			{
				continue;
			}
			Indices.Add(A); Indices.Add(C); Indices.Add(B);
			Indices.Add(B); Indices.Add(C); Indices.Add(D);
		}
	}

	// Face normals + slope colors. Do not use radial N — that made far PBR sample
	// the 2 m grass/volcanic atlas on 72 m triangles (red tiled sheet).
	TArray<FVector> AccN;
	AccN.Init(FVector::ZeroVector, Positions.Num());
	for (int32 T0 = 0; T0 + 2 < Indices.Num(); T0 += 3)
	{
		const int32 IA = Indices[T0], IB = Indices[T0 + 1], IC = Indices[T0 + 2];
		const FVector FN = FVector::CrossProduct(Positions[IB] - Positions[IA], Positions[IC] - Positions[IA]);
		AccN[IA] += FN; AccN[IB] += FN; AccN[IC] += FN;
	}
	for (int32 V = 0; V < Positions.Num(); ++V)
	{
		FVector N = AccN[V].GetSafeNormal();
		if (N.IsNearlyZero())
		{
			N = Normals[V];
		}
		Normals[V] = N;
		const FVector Radial = Positions[V].GetSafeNormal();
		const float Slope = 1.0f - FMath::Abs(FVector::DotProduct(N, Radial));
		if (Slope < 0.14f)
		{
			Colors[V] = FLinearColor(0.30f, 0.42f, 0.22f); // plains grass
		}
		else if (Slope < 0.32f)
		{
			Colors[V] = FLinearColor(0.46f, 0.34f, 0.20f); // dirt skirt
		}
		else
		{
			Colors[V] = FLinearColor(0.50f, 0.48f, 0.46f); // rock, not cyan
		}
	}

	Comp->ClearAllMeshSections();
	if (Positions.Num() >= 3 && Indices.Num() >= 3)
	{
		Comp->CreateMeshSection_LinearColor(0, Positions, Indices, Normals, UV0, Colors, Tangents, false);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetVisibility(true);
	}
}

void FGXHorizonClipmap::Update(
	AActor* Owner,
	const FGXSphereStamp& Stamp,
	const FVector& ViewerLocalM,
	float InnerHoleM,
	float OuterM,
	UMaterialInterface* NearMaterial,
	UMaterialInterface* FarMaterial)
{
	if (!Owner || Rings.Num() == 0)
	{
		return;
	}
	if (FVector::DistSquared(ViewerLocalM, LastViewerLocal) < FMath::Square(400.0f) && bReady)
	{
		return;
	}
	LastViewerLocal = ViewerLocalM;
	const double T0 = FPlatformTime::Seconds();

	FVector CenterDir = ViewerLocalM.GetSafeNormal();
	if (CenterDir.IsNearlyZero())
	{
		CenterDir = FVector(1, 0, 0);
	}
	FVector T, B;
	CenterDir.FindBestAxisVectors(T, B);

	// Only punch a hole underfoot. A 300 m inner hole was the black pit in
	// the 0.7.7 shot — missing voxels opened a view into the planet.
	// Clipmap sits 1.5 m under the stamp so overlapping voxels win depth.
	(void)InnerHoleM;
	Rings[0].InnerM = 48.0f;
	if (Rings.Num() > 2)
	{
		Rings.Last().OuterM = FMath::Max(OuterM, 4000.0f);
	}

	UMaterialInterface* FarLit = FarMaterial;
	if (!FarLit)
	{
		FarLit = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Voxel/Materials/M_VoxelHorizonFar.M_VoxelHorizonFar"));
	}
	if (!FarLit)
	{
		FarLit = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}
	for (int32 I = 0; I < Rings.Num(); ++I)
	{
		FRing& Ring = Rings[I];
		if (UProceduralMeshComponent* C = Ring.Comp.Get())
		{
			BuildRing(C, Stamp, CenterDir, T, B, Ring.InnerM, Ring.OuterM, Ring.CellM, FarLit);
		}
	}
	bReady = true;
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap rebuilt inner=%.0f outer=%.0f ms=%.1f"),
		Rings[0].InnerM, Rings.Last().OuterM, Ms);
	GX_PERF(1, TEXT("GX-clipmap rebuild ms=%.1f inner=%.0f outer=%.0f rings=%d"),
		Ms, Rings[0].InnerM, Rings.Last().OuterM, Rings.Num());
}
