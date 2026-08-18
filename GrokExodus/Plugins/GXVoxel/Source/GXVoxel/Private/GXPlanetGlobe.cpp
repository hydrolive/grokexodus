// Copyright Grok Exodus. All Rights Reserved.

#include "GXPlanetGlobe.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"

void FGXPlanetGlobe::Shutdown()
{
	if (UProceduralMeshComponent* C = Comp.Get())
	{
		C->DestroyComponent();
	}
	Comp.Reset();
	Positions.Reset();
	Normals.Reset();
	UV0.Reset();
	Colors.Reset();
	Tangents.Reset();
	Indices.Reset();
	LiveIndices.Reset();
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

	// 6×80². ~0.75° / 0.8 km cells. Sink under near-field so digs do not
	// need a 1 km punch (that cut orbital holes through the crust).
	constexpr int32 N = 80;
	constexpr float SinkM = 80.0f;
	const float R0 = Stamp.GetParams().Radius;
	const float Relief = FMath::Max(1.0f, Stamp.GetParams().MaxRelief);
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

	auto Sample = [&Stamp](const FVector& Dir) -> float
	{
		return Stamp.SampleSurfaceRadius(FVector3f(Dir.X, Dir.Y, Dir.Z));
	};

	auto Biome = [R0, Relief](float Surf, float Slope) -> FLinearColor
	{
		const float Alt = (Surf - R0) / Relief;
		if (Alt < -0.05f)
		{
			return FLinearColor(0.16f, 0.26f, 0.28f, 1.0f);
		}
		if (Alt < 0.015f)
		{
			return FLinearColor(0.30f, 0.44f, 0.28f, 1.0f);
		}
		if (Slope > 0.18f || Alt > 0.22f)
		{
			return FLinearColor(0.50f, 0.46f, 0.40f, 1.0f);
		}
		if (Slope > 0.10f)
		{
			return FLinearColor(0.48f, 0.38f, 0.26f, 1.0f);
		}
		return FLinearColor(0.40f, 0.50f, 0.28f, 1.0f);
	};

	const float Eps = 0.0035f;
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
				const float Surf = Sample(Dir);
				FVector Tangent, Bitangent;
				Dir.FindBestAxisVectors(Tangent, Bitangent);
				const FVector Dt = (Dir + Tangent * Eps).GetSafeNormal();
				const FVector Db = (Dir + Bitangent * Eps).GetSafeNormal();
				const float Rt = Sample(Dt);
				const float Rb = Sample(Db);
				const FVector P = Dir * Surf;
				FVector NrmS = FVector::CrossProduct(Dt * Rt - P, Db * Rb - P).GetSafeNormal();
				if (NrmS.IsNearlyZero() || FVector::DotProduct(NrmS, Dir) < 0.0f)
				{
					NrmS = Dir;
				}
				const float Slope = 1.0f - FMath::Abs(FVector::DotProduct(NrmS, Dir));
				Pos.Add(Dir * (Surf - SinkM) * 100.0f);
				Nrm.Add(NrmS);
				UV.Add(FVector2D(0.0f, 0.0f));
				Col.Add(Biome(Surf, Slope));
				FVector T = FVector::CrossProduct(NrmS, FVector::ZAxisVector);
				if (T.SizeSquared() < 1e-6f)
				{
					T = FVector::CrossProduct(NrmS, FVector::YAxisVector);
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
	Positions = MoveTemp(Pos);
	Normals = MoveTemp(Nrm);
	UV0 = MoveTemp(UV);
	Colors = MoveTemp(Col);
	Tangents = MoveTemp(Tan);
	Indices = MoveTemp(Idx);
	LiveIndices = Indices;
	PMC->CreateMeshSection_LinearColor(0, Positions, LiveIndices, Normals, UV0, Colors, Tangents, false);
	if (Material)
	{
		PMC->SetMaterial(0, Material);
	}
	bReady = true;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXPlanetGlobe ready verts=%d (stamp crust, sink=%.0fm, no punch)"),
		Positions.Num(), SinkM);
	GX_PERF(1, TEXT("GX-globe verts=%d sink=%.0f far-mat"), Positions.Num(), SinkM);
}

int32 FGXPlanetGlobe::PunchIsland(const FGXEditIsland& Island, UMaterialInterface* Material)
{
	(void)Island;
	(void)Material;
	return 0;
}
