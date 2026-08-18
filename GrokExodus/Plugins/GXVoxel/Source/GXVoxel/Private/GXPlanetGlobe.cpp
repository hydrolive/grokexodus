// Copyright Grok Exodus. All Rights Reserved.

#include "GXPlanetGlobe.h"
#include "GXVoxel.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"

void FGXPlanetGlobe::Shutdown()
{
	if (UProceduralMeshComponent* C = Comp.Get())
	{
		C->DestroyComponent();
	}
	Comp.Reset();
	bReady = false;
}

void FGXPlanetGlobe::Ensure(AActor* Owner, const FGXSphereStamp& Stamp, UMaterialInterface* Material)
{
	if (!Owner || bReady)
	{
		return;
	}
	UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
	if (!PMC)
	{
		return;
	}
	PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PMC->SetCastShadow(false);
	PMC->SetVisibleInRayTracing(false);
	PMC->bNeverDistanceCull = true;
	PMC->SetCullDistance(0.0f);
	PMC->SetBoundsScale(32.0f);
	PMC->SetupAttachment(Owner->GetRootComponent());
	PMC->RegisterComponent();
	Comp = PMC;

	// 6×20² = 2400 quads. ~3° cells — mountains read from orbit, cheap to cook.
	constexpr int32 N = 20;
	constexpr float SinkM = 40.0f;
	const float R0 = Stamp.GetParams().Radius;
	TArray<FVector> Pos, Nrm;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	TArray<int32> Idx;
	Pos.Reserve(6 * (N + 1) * (N + 1));

	auto FaceDir = [](int32 Face, float U, float V) -> FVector
	{
		const float X = U * 2.f - 1.f;
		const float Y = V * 2.f - 1.f;
		switch (Face)
		{
		case 0: return FVector(1, X, Y);
		case 1: return FVector(-1, -X, Y);
		case 2: return FVector(X, 1, Y);
		case 3: return FVector(-X, -1, Y);
		case 4: return FVector(X, Y, 1);
		default: return FVector(X, -Y, -1);
		}
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 Base = Pos.Num();
		for (int32 J = 0; J <= N; ++J)
		{
			for (int32 I = 0; I <= N; ++I)
			{
				const float U = static_cast<float>(I) / static_cast<float>(N);
				const float V = static_cast<float>(J) / static_cast<float>(N);
				FVector Dir = FaceDir(Face, U, V).GetSafeNormal();
				const float Surf = Stamp.SampleSurfaceRadius(FVector3f(Dir.X, Dir.Y, Dir.Z));
				Pos.Add(Dir * (Surf - SinkM) * 100.0f);
				Nrm.Add(Dir);
				UV.Add(FVector2D(1.0f, 0.0f));
				Col.Add(FLinearColor(0.45f, 0.52f, 0.30f, 1.0f));
				FVector T = FVector::CrossProduct(Dir, FVector::ZAxisVector);
				if (T.SizeSquared() < 1e-6f)
				{
					T = FVector::CrossProduct(Dir, FVector::YAxisVector);
				}
				Tan.Add(FProcMeshTangent(T.GetSafeNormal(), false));
			}
		}
		const int32 Stride = N + 1;
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const int32 A = Base + I + J * Stride;
				const int32 Bv = A + 1;
				const int32 C = A + Stride;
				const int32 D = C + 1;
				Idx.Add(A); Idx.Add(C); Idx.Add(Bv);
				Idx.Add(Bv); Idx.Add(C); Idx.Add(D);
			}
		}
	}
	(void)R0;
	PMC->CreateMeshSection_LinearColor(0, Pos, Idx, Nrm, UV, Col, Tan, false);
	if (Material)
	{
		PMC->SetMaterial(0, Material);
	}
	bReady = true;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXPlanetGlobe ready verts=%d (stamp crust, sink=%.0fm)"),
		Pos.Num(), SinkM);
}
