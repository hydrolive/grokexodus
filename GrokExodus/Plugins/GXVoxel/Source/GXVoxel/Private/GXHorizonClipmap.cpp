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

	struct FSpec { float Inner; float Outer; float Cell; float Sink; };
	// Overlap ~150 m so rings never leave a sky gap. Outer rings sit a
	// little deeper so the shared band does not z-fight.
	const FSpec Specs[] = {
		{ 0.0f, 1800.0f, 20.0f, 12.0f },
		{ 1650.0f, 4800.0f, 40.0f, 14.0f },
		{ 4500.0f, 10000.0f, 80.0f, 16.0f },
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
		PMC->bNeverDistanceCull = true;
		PMC->SetCullDistance(0.0f);
		PMC->SetBoundsScale(8.0f);
		PMC->SetupAttachment(Owner->GetRootComponent());
		PMC->RegisterComponent();
		FRing Ring;
		Ring.Comp = PMC;
		Ring.InnerM = S.Inner;
		Ring.OuterM = S.Outer;
		Ring.CellM = S.Cell;
		Ring.SinkM = S.Sink;
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
	float SinkM,
	UMaterialInterface* Material)
{
	if (!Comp)
	{
		return;
	}

	const int32 Half = FMath::Max(8, FMath::CeilToInt(OuterM / CellM));
	const int32 Dim = Half * 2 + 1;
	const float R0 = Stamp.GetParams().Radius;
	const float Relief = FMath::Max(Stamp.GetParams().MaxRelief, 1.0f);
	const float InnerPad = InnerM * InnerM;
	const float OuterPad = OuterM * OuterM;
	const float Sink = FMath::Max(SinkM, 0.5f);

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
			const FGXEarthField Field = Stamp.SampleEarthField(Df, false);
			const float SurfR = Stamp.GetParams().Radius + Field.HeightM;
			// Sit under LOD1/2 MC (2–4 m voxels) so the clipmap never
			// pokes through as a dark cap. Missing voxels still show crust.
			const FVector P = Dir * (SurfR - Sink) * 100.0f;
			const int32 Idx = I + J * Dim;
			IndexOf[Idx] = Positions.Num();
			Positions.Add(P);
			Normals.Add(Dir);
			float Biome = 1.0f;
			if (Field.Volcano > 0.08f)
			{
				Biome = (Field.HeightM > 1280.0f) ? 5.0f : 7.0f;
			}
			else if (Field.Orogeny > 0.04f || Field.HeightM > 280.0f || Field.SlopeProxy > 0.18f)
			{
				Biome = 2.0f;
			}
			UV0.Add(FVector2D(Biome, 0.0f));
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
			if (CD2 < InnerPad * 0.82f || CD2 > OuterPad * 1.10f)
			{
				continue;
			}
			// Outward winding. A,C,B faced the core — 0.7.15 mid-range was
			// backface-culled (teal void) and only the underside showed.
			Indices.Add(A); Indices.Add(B); Indices.Add(C);
			Indices.Add(B); Indices.Add(D); Indices.Add(C);
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
		const FVector Radial = Positions[V].GetSafeNormal();
		if (FVector::DotProduct(N, Radial) < 0.0f)
		{
			N = -N;
		}
		Normals[V] = N;
		const float Slope = 1.0f - FMath::Abs(FVector::DotProduct(N, Radial));
		const float HeightM = Positions[V].Size() * 0.01f + Sink - R0;
		const float Alt = HeightM / Relief;
		const float Biome = UV0[V].X;
		if (Biome > 4.5f && Biome < 5.5f)
		{
			Colors[V] = FLinearColor(0.90f, 0.92f, 0.94f); // snow
		}
		else if (Biome > 6.5f)
		{
			Colors[V] = FLinearColor(0.42f, 0.32f, 0.28f); // volcanic
		}
		else if (Biome > 1.5f || Alt > 0.18f || Slope > 0.16f)
		{
			Colors[V] = FLinearColor(0.64f, 0.56f, 0.48f); // rock
		}
		else if (Slope > 0.10f)
		{
			Colors[V] = FLinearColor(0.56f, 0.44f, 0.28f); // dirt skirt
		}
		else
		{
			Colors[V] = FLinearColor(0.46f, 0.60f, 0.30f); // grass plains
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
		Comp->SetHiddenInGame(false);
		Comp->UpdateBounds();
		GX_PERF(2, TEXT("GX-clipmap ring inner=%.0f outer=%.0f verts=%d tris=%d sink=%.1f"),
			InnerM, OuterM, Positions.Num(), Indices.Num() / 3, Sink);
	}
	else
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap empty ring inner=%.0f outer=%.0f"), InnerM, OuterM);
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
	if (FVector::DistSquared(ViewerLocalM, LastViewerLocal) < FMath::Square(250.0f) && bReady)
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

	// Full disk, sunk ~12 m. A 280 m hole (0.7.17) was the teal void;
	// a 3.5 m sink was the dig-pond. Voxels win underfoot; clipmap fills gaps.
	(void)InnerHoleM;
	if (Rings.Num() > 0)
	{
		Rings[0].InnerM = 0.0f;
	}
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
		const float RebuildM = FMath::Max(500.0f, Ring.CellM * 18.0f);
		if (bReady && FVector::DistSquared(ViewerLocalM, Ring.LastBuild) < FMath::Square(RebuildM))
		{
			continue;
		}
		if (UProceduralMeshComponent* C = Ring.Comp.Get())
		{
			BuildRing(C, Stamp, CenterDir, T, B, Ring.InnerM, Ring.OuterM, Ring.CellM, Ring.SinkM, FarLit);
			Ring.LastBuild = ViewerLocalM;
		}
	}
	bReady = true;
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap rebuilt inner=%.0f outer=%.0f ms=%.1f"),
		Rings[0].InnerM, Rings.Last().OuterM, Ms);
	GX_PERF(1, TEXT("GX-clipmap rebuild ms=%.1f inner=%.0f outer=%.0f rings=%d"),
		Ms, Rings[0].InnerM, Rings.Last().OuterM, Rings.Num());
}
