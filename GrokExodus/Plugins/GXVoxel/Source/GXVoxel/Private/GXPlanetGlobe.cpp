// Copyright Grok Exodus. All Rights Reserved.

#include "GXPlanetGlobe.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
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
	PMC->SetBoundsScale(64.0f);
	PMC->SetReceivesDecals(false);
	PMC->SetupAttachment(Owner->GetRootComponent());
	PMC->RegisterComponent();
	Comp = PMC;

	// 6×128². ~0.74 km cells. Sink under tiles. Do not punch.
	constexpr int32 N = 128;
	constexpr float SinkM = 12.0f;
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

	auto Biome = [](int32 Layer) -> FLinearColor
	{
		switch (Layer)
		{
		case 2: return FLinearColor(0.50f, 0.46f, 0.40f, 1.0f); // rock
		case 3: return FLinearColor(0.55f, 0.40f, 0.24f, 1.0f); // dirt
		case 4: return FLinearColor(0.84f, 0.72f, 0.42f, 1.0f); // sand
		case 5: return FLinearColor(0.78f, 0.86f, 0.92f, 1.0f); // ice
		case 6: return FLinearColor(0.30f, 0.24f, 0.16f, 1.0f); // mud
		case 7: return FLinearColor(0.28f, 0.24f, 0.22f, 1.0f); // volcanic
		default: return FLinearColor(0.28f, 0.48f, 0.20f, 1.0f); // grass
		}
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
				// Keep a little slope for dirt skirts; mostly radial so PBR
				// does not treat every km cell as a cliff (tan rock sheet).
				NrmS = (NrmS * 0.15f + Dir * 0.85f).GetSafeNormal();
				const int32 Layer = Stamp.SampleSurfaceMaterial(FVector3f(Dir.X, Dir.Y, Dir.Z));
				Pos.Add(Dir * (Surf - SinkM) * 100.0f);
				Nrm.Add(Dir);
				UV.Add(FVector2D(static_cast<float>(Layer), Surf));
				Col.Add(Biome(Layer));
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
				// Same order as walk tiles: A-C-B is clockwise from the sky
				// (Cross toward the core). A-B-C was culled on +X (0.9.10).
				Idx.Add(A); Idx.Add(C); Idx.Add(Bv);
				Idx.Add(Bv); Idx.Add(C); Idx.Add(D);
			}
		}
	}
	// Cube faces do not share a parametric orientation. Flip any tri
	// whose geometric normal points at the core (0.12.9 A-C-B opened
	// whole faces to the sky — shots 204051 / 204114).
	int32 Flipped = 0;
	for (int32 T = 0; T + 2 < Idx.Num(); T += 3)
	{
		const int32 IA = Idx[T], IB = Idx[T + 1], IC = Idx[T + 2];
		if (!Pos.IsValidIndex(IA) || !Pos.IsValidIndex(IB) || !Pos.IsValidIndex(IC))
		{
			continue;
		}
		const FVector FN = FVector::CrossProduct(Pos[IB] - Pos[IA], Pos[IC] - Pos[IA]);
		// UE front face wants Cross toward the core (tile comment 0.9.10).
		if (FVector::DotProduct(FN, Pos[IA]) > 0.0f)
		{
			Swap(Idx[T + 1], Idx[T + 2]);
			++Flipped;
		}
	}
	Positions = MoveTemp(Pos);
	Normals = MoveTemp(Nrm);
	UV0 = MoveTemp(UV);
	Colors = MoveTemp(Col);
	Tangents = MoveTemp(Tan);
	Indices = MoveTemp(Idx);
	LiveIndices = Indices;
	UMaterialInterface* UseMat = Material;
	if (!UseMat)
	{
		UseMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Voxel/Materials/M_VoxelTerrain_VertexColor.M_VoxelTerrain_VertexColor"));
	}
	PMC->CreateMeshSection_LinearColor(0, Positions, LiveIndices, Normals, UV0, Colors, Tangents, false);
	if (UseMat)
	{
		PMC->SetMaterial(0, UseMat);
	}
	PMC->UpdateBounds();
	bReady = true;
	int32 Hist[8] = {};
	float HMin = 1.0e9f, HMax = -1.0e9f;
	for (int32 I = 0; I < UV0.Num(); ++I)
	{
		const int32 L = FMath::Clamp(FMath::RoundToInt(UV0[I].X), 0, 7);
		++Hist[L];
		const float H = Positions[I].Size() * 0.01f + SinkM - R0;
		HMin = FMath::Min(HMin, H);
		HMax = FMath::Max(HMax, H);
	}
	UE_LOG(LogGXVoxel, Warning,
		TEXT("GXPlanetGlobe ready verts=%d sink=%.0fm n=%d wind=ACB flip=%d h=%.0f..%.0f ice=%d sand=%d grass=%d dirt=%d rock=%d mud=%d mat=%s"),
		Positions.Num(), SinkM, N, Flipped, HMin, HMax,
		Hist[5], Hist[4], Hist[1], Hist[3], Hist[2], Hist[6], *GetNameSafe(UseMat));
	GX_PERF(1, TEXT("GX-globe verts=%d n=%d h=%.0f..%.0f ice=%d sand=%d grass=%d"),
		Positions.Num(), N, HMin, HMax, Hist[5], Hist[4], Hist[1]);
}

int32 FGXPlanetGlobe::PunchIsland(const FGXEditIsland& Island, UMaterialInterface* Material)
{
	(void)Island;
	(void)Material;
	return 0;
}
