// Copyright Grok Exodus. All Rights Reserved.

#include "GXHorizonClipmap.h"
#include "GXVoxel.h"
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
		{ 160.0f, 1200.0f, 20.0f },
		{ 1100.0f, 3500.0f, 48.0f },
		{ 3300.0f, 8000.0f, 96.0f },
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
			if (D2 < InnerPad * 0.85f || D2 > OuterPad * 1.05f)
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
			const FVector P = Dir * SurfR * 100.0f; // world cm, planet at origin
			const int32 Idx = I + J * Dim;
			IndexOf[Idx] = Positions.Num();
			Positions.Add(P);
			Normals.Add(Dir);
			UV0.Add(FVector2D(1.0f, 0.0f)); // grass/dirt; slope blend is in the material
			Colors.Add(FLinearColor(0.45f, 0.55f, 0.32f, 1.0f));
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
			Indices.Add(A); Indices.Add(C); Indices.Add(B);
			Indices.Add(B); Indices.Add(C); Indices.Add(D);
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
	UMaterialInterface* Material)
{
	if (!Owner || Rings.Num() == 0)
	{
		return;
	}
	if (FVector::DistSquared(ViewerLocalM, LastViewerLocal) < FMath::Square(80.0f) && bReady)
	{
		return;
	}
	LastViewerLocal = ViewerLocalM;

	FVector CenterDir = ViewerLocalM.GetSafeNormal();
	if (CenterDir.IsNearlyZero())
	{
		CenterDir = FVector(1, 0, 0);
	}
	FVector T, B;
	CenterDir.FindBestAxisVectors(T, B);

	Rings[0].InnerM = FMath::Max(80.0f, InnerHoleM * 0.75f);
	if (Rings.Num() > 2)
	{
		Rings.Last().OuterM = FMath::Max(OuterM, 4000.0f);
	}

	for (FRing& Ring : Rings)
	{
		if (UProceduralMeshComponent* C = Ring.Comp.Get())
		{
			BuildRing(C, Stamp, CenterDir, T, B, Ring.InnerM, Ring.OuterM, Ring.CellM, Material);
		}
	}
	bReady = true;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap rebuilt inner=%.0f outer=%.0f"),
		Rings[0].InnerM, Rings.Last().OuterM);
}
