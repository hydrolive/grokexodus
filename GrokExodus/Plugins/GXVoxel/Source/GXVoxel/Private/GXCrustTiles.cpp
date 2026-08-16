// Copyright Grok Exodus. All Rights Reserved.

#include "GXCrustTiles.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"

namespace
{
	UProceduralMeshComponent* MakeTilePMC(AActor* Owner)
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
		if (!PMC)
		{
			return nullptr;
		}
		PMC->bUseComplexAsSimpleCollision = true;
		PMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PMC->SetCollisionObjectType(ECC_WorldStatic);
		PMC->SetCollisionResponseToAllChannels(ECR_Block);
		PMC->SetCastShadow(false);
		PMC->SetVisibleInRayTracing(false);
		PMC->bUseAsyncCooking = false;
		PMC->bNeverDistanceCull = true;
		PMC->SetCullDistance(0.0f);
		PMC->SetBoundsScale(4.0f);
		PMC->SetupAttachment(Owner->GetRootComponent());
		PMC->RegisterComponent();
		return PMC;
	}
}

void FGXCrustTiles::Initialize(AActor* Owner)
{
	Shutdown();
	OwnerCached = Owner;
	bReady = false;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles: tile=%.0f cell=%.0f stream=%.0f (0.9 crust)"),
		TileM, CellM, StreamM);
}

void FGXCrustTiles::Shutdown()
{
	for (auto& Pair : Live)
	{
		if (UProceduralMeshComponent* C = Pair.Value.Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	Live.Reset();
	HiddenKeys.Reset();
	OwnerCached = nullptr;
	bReady = false;
}

int8 FGXCrustTiles::FaceOf(const FVector& Dir)
{
	const FVector A(FMath::Abs(Dir.X), FMath::Abs(Dir.Y), FMath::Abs(Dir.Z));
	if (A.X >= A.Y && A.X >= A.Z)
	{
		return Dir.X >= 0.0f ? 0 : 1;
	}
	if (A.Y >= A.Z)
	{
		return Dir.Y >= 0.0f ? 2 : 3;
	}
	return Dir.Z >= 0.0f ? 4 : 5;
}

void FGXCrustTiles::FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB)
{
	switch (Face)
	{
	case 0: OutN = FVector(1, 0, 0); OutT = FVector(0, 1, 0); OutB = FVector(0, 0, 1); break;
	case 1: OutN = FVector(-1, 0, 0); OutT = FVector(0, -1, 0); OutB = FVector(0, 0, 1); break;
	case 2: OutN = FVector(0, 1, 0); OutT = FVector(-1, 0, 0); OutB = FVector(0, 0, 1); break;
	case 3: OutN = FVector(0, -1, 0); OutT = FVector(1, 0, 0); OutB = FVector(0, 0, 1); break;
	case 4: OutN = FVector(0, 0, 1); OutT = FVector(0, 1, 0); OutB = FVector(-1, 0, 0); break;
	default: OutN = FVector(0, 0, -1); OutT = FVector(0, 1, 0); OutB = FVector(1, 0, 0); break;
	}
}

FGXCrustTileKey FGXCrustTiles::KeyAt(const FVector& LocalM, int32 LOD)
{
	FGXCrustTileKey K;
	K.LOD = LOD;
	const FVector Dir = LocalM.GetSafeNormal();
	K.Face = FaceOf(Dir);
	FVector N, T, B;
	FaceAxes(K.Face, N, T, B);
	const float Scale = TileM * static_cast<float>(1 << FMath::Max(0, LOD));
	K.U = FMath::FloorToInt(FVector::DotProduct(LocalM, T) / Scale);
	K.V = FMath::FloorToInt(FVector::DotProduct(LocalM, B) / Scale);
	return K;
}

void FGXCrustTiles::HideTile(const FGXCrustTileKey& Key)
{
	HiddenKeys.Add(Key);
	if (FTile* T = Live.Find(Key))
	{
		if (UProceduralMeshComponent* C = T->Comp.Get())
		{
			C->DestroyComponent();
		}
		Live.Remove(Key);
	}
}

int32 FGXCrustTiles::HideTilesInSphere(const FVector& LocalM, float RadiusM)
{
	const float Cover = FMath::Max(RadiusM, 0.5f) + TileM * 0.65f;
	const float Cover2 = Cover * Cover;
	TArray<FGXCrustTileKey> Hit;
	for (const auto& Pair : Live)
	{
		if (HiddenKeys.Contains(Pair.Key))
		{
			continue;
		}
		FVector N, T, B;
		FaceAxes(Pair.Key.Face, N, T, B);
		const float Scale = TileM * static_cast<float>(1 << FMath::Max(0, Pair.Key.LOD));
		const float CU = (static_cast<float>(Pair.Key.U) + 0.5f) * Scale;
		const float CV = (static_cast<float>(Pair.Key.V) + 0.5f) * Scale;
		const FVector Approx = N * LocalM.Size() + T * CU + B * CV;
		if (FVector::DistSquared(Approx, LocalM) <= Cover2)
		{
			Hit.Add(Pair.Key);
		}
	}
	const FGXCrustTileKey Center = KeyAt(LocalM, 0);
	for (int32 DV = -1; DV <= 1; ++DV)
	{
		for (int32 DU = -1; DU <= 1; ++DU)
		{
			FGXCrustTileKey K = Center;
			K.U += DU;
			K.V += DV;
			if (!HiddenKeys.Contains(K))
			{
				Hit.AddUnique(K);
			}
		}
	}
	int32 N = 0;
	for (const FGXCrustTileKey& K : Hit)
	{
		FVector Nrm, T, B;
		FaceAxes(K.Face, Nrm, T, B);
		const float Scale = TileM * static_cast<float>(1 << FMath::Max(0, K.LOD));
		const float CU = (static_cast<float>(K.U) + 0.5f) * Scale;
		const float CV = (static_cast<float>(K.V) + 0.5f) * Scale;
		const FVector Approx = Nrm * LocalM.Size() + T * CU + B * CV;
		if (FVector::DistSquared(Approx, LocalM) > Cover2)
		{
			continue;
		}
		HideTile(K);
		++N;
	}
	if (N > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles hide=%d at (%.0f,%.0f,%.0f) r=%.1f"),
			N, LocalM.X, LocalM.Y, LocalM.Z, RadiusM);
	}
	return N;
}

int32 FGXCrustTiles::NotifyBrush(const FVector& LocalM, float RadiusM, bool bRemove)
{
	if (RadiusM <= 0.0f || Live.Num() == 0)
	{
		return 0;
	}
	const FVector BrushDir = LocalM.GetSafeNormal();
	if (BrushDir.IsNearlyZero())
	{
		return 0;
	}
	const float BrushSurf = LocalM.Size();
	const float R = RadiusM;
	const float R2 = R * R;
	int32 Changed = 0;
	for (auto& Pair : Live)
	{
		FTile& Tile = Pair.Value;
		UProceduralMeshComponent* Comp = Tile.Comp.Get();
		if (!Comp || Tile.LivePos.Num() == 0 || Tile.StampDir.Num() != Tile.LivePos.Num())
		{
			continue;
		}
		const FVector TileWorld = Tile.OriginCm * 0.01f;
		if (FVector::DistSquared(TileWorld, LocalM) > FMath::Square(R + TileM + 4.0f))
		{
			continue;
		}
		int32 N = 0;
		for (int32 I = 0; I < Tile.LivePos.Num(); ++I)
		{
			const FVector Dir = Tile.StampDir[I];
			const float Surf = Tile.StampSurfM.IsValidIndex(I) ? Tile.StampSurfM[I] : (Tile.OriginCm + Tile.LivePos[I]).Size() * 0.01f;
			const FVector P = Dir * Surf;
			const float D2 = FVector::DistSquared(P, BrushDir * BrushSurf);
			if (D2 > R2)
			{
				continue;
			}
			const float D = FMath::Sqrt(D2);
			const float Rise = FMath::Sqrt(FMath::Max(R2 - D2, 0.0f));
			float NewR = Surf;
			if (bRemove)
			{
				NewR = BrushSurf - Rise;
			}
			else
			{
				NewR = BrushSurf + Rise;
			}
			NewR = FMath::Clamp(NewR, Surf - R - 1.0f, Surf + R + 1.0f);
			const FVector NewP = Dir * NewR * 100.0f - Tile.OriginCm;
			if ((NewP - Tile.LivePos[I]).SizeSquared() < 1.0f)
			{
				continue;
			}
			Tile.LivePos[I] = NewP;
			if (bRemove)
			{
				if (Tile.UV0.IsValidIndex(I))
				{
					Tile.UV0[I] = FVector2D(2.0f, 0.0f);
				}
				if (Tile.Colors.IsValidIndex(I))
				{
					Tile.Colors[I] = FLinearColor(0.58f, 0.50f, 0.44f, 1.0f);
				}
			}
			++N;
		}
		if (N == 0)
		{
			continue;
		}
		Comp->CreateMeshSection_LinearColor(
			0, Tile.LivePos, Tile.Indices, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents, true);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->UpdateBounds();
		Changed += N;
	}
	if (Changed > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles brush %s verts=%d r=%.2f"),
			bRemove ? TEXT("dig") : TEXT("place"), Changed, RadiusM);
	}
	return Changed;
}

bool FGXCrustTiles::HasTileAt(const FVector& LocalM) const
{
	return Live.Contains(KeyAt(LocalM, 0));
}

bool FGXCrustTiles::HasNeighborhood(const FVector& LocalM, int32 Half) const
{
	const FGXCrustTileKey C = KeyAt(LocalM, 0);
	const int32 H = FMath::Max(0, Half);
	for (int32 DV = -H; DV <= H; ++DV)
	{
		for (int32 DU = -H; DU <= H; ++DU)
		{
			FGXCrustTileKey K = C;
			K.U += DU;
			K.V += DV;
			if (!Live.Contains(K))
			{
				return false;
			}
		}
	}
	return true;
}

void FGXCrustTiles::BuildTile(FTile& Tile, const FGXSphereStamp& Stamp, UMaterialInterface* Material,
	const TFunction<float(const FVector&)>& DensityAt)
{
	UProceduralMeshComponent* Comp = Tile.Comp.Get();
	if (!Comp)
	{
		return;
	}
	const float Scale = TileM * static_cast<float>(1 << FMath::Max(0, Tile.Key.LOD));
	const float Cell = CellM * static_cast<float>(1 << FMath::Max(0, Tile.Key.LOD));
	const int32 Cells = FMath::Max(2, FMath::RoundToInt(Scale / Cell));
	const int32 Dim = Cells + 1;
	FVector FaceN, T, B;
	FaceAxes(Tile.Key.Face, FaceN, T, B);
	const float R0 = Stamp.GetParams().Radius;
	const float OriginU = static_cast<float>(Tile.Key.U) * Scale;
	const float OriginV = static_cast<float>(Tile.Key.V) * Scale;

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Indices;
	TArray<FVector> StampDir;
	TArray<float> StampSurfM;
	Positions.Reserve(Dim * Dim);
	Normals.Reserve(Dim * Dim);
	UV0.Reserve(Dim * Dim);
	Colors.Reserve(Dim * Dim);
	Tangents.Reserve(Dim * Dim);
	StampDir.Reserve(Dim * Dim);
	StampSurfM.Reserve(Dim * Dim);

	for (int32 J = 0; J < Dim; ++J)
	{
		const float V = OriginV + static_cast<float>(J) * Cell;
		for (int32 I = 0; I < Dim; ++I)
		{
			const float U = OriginU + static_cast<float>(I) * Cell;
			FVector Dir = (FaceN * R0 + T * U + B * V).GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = FaceN;
			}
			const FGXEarthField Field = Stamp.SampleEarthField(FVector3f(Dir.X, Dir.Y, Dir.Z), false);
			float SurfR = Stamp.GetParams().Radius + Field.HeightM;
			if (DensityAt)
			{
				const float D0 = DensityAt(Dir * SurfR);
				if (D0 > 0.05f)
				{
					for (float D = 0.25f; D <= 8.0f; D += 0.25f)
					{
						if (DensityAt(Dir * (SurfR + D)) <= 0.0f)
						{
							SurfR += D;
							break;
						}
					}
				}
				else if (D0 < -0.05f)
				{
					for (float D = 0.25f; D <= 8.0f; D += 0.25f)
					{
						if (DensityAt(Dir * (SurfR - D)) > 0.0f)
						{
							SurfR -= D;
							break;
						}
					}
				}
			}
			Positions.Add(Dir * SurfR * 100.0f);
			StampDir.Add(Dir);
			StampSurfM.Add(SurfR);
			Normals.Add(Dir);
			UV0.Add(FVector2D(1.0f, 0.0f));
			const float Slope = 0.0f;
			(void)Slope;
			Colors.Add(FLinearColor(0.58f, 0.66f, 0.38f, 1.0f));
			FVector Tan = FVector::CrossProduct(Dir, FVector::ZAxisVector);
			if (Tan.SizeSquared() < 1e-6f)
			{
				Tan = FVector::CrossProduct(Dir, FVector::YAxisVector);
			}
			Tan.Normalize();
			Tangents.Add(FProcMeshTangent(Tan, false));
		}
	}

	// UE/D3D front faces are clockwise from the sky (Cross toward the core).
	// A,B,C faces outward on +X and was culled — the 0.9.10 hole showed
	// the planet interior because the walk tiles were invisible.
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			const int32 A = I + J * Dim;
			const int32 Bv = (I + 1) + J * Dim;
			const int32 C = I + (J + 1) * Dim;
			const int32 D = (I + 1) + (J + 1) * Dim;
			Indices.Add(A); Indices.Add(C); Indices.Add(Bv);
			Indices.Add(Bv); Indices.Add(C); Indices.Add(D);
		}
	}
	const float Relief = FMath::Max(Stamp.GetParams().MaxRelief, 1.0f);
	for (int32 VI = 0; VI < Positions.Num(); ++VI)
	{
		// Radial N so shared tile edges light the same (face AccN was a crease).
		const FVector Radial = Positions[VI].GetSafeNormal();
		Normals[VI] = Radial.IsNearlyZero() ? FaceN : Radial;
		const float HeightM = Positions[VI].Size() * 0.01f - R0;
		const float Alt = HeightM / Relief;
		if (Alt > 0.16f)
		{
			Colors[VI] = FLinearColor(0.58f, 0.50f, 0.44f);
			UV0[VI] = FVector2D(2.0f, 0.0f);
		}
	}

	// Verts live on the tile, not 60 km from the planet origin.
	// Planet-sized PMC bounds made collision a no-op (0.9.1 still fell through).
	const int32 Mid = (Dim / 2) + (Dim / 2) * Dim;
	const FVector TileOrigin = Positions.IsValidIndex(Mid) ? Positions[Mid] : Positions[0];
	for (FVector& P : Positions)
	{
		P -= TileOrigin;
	}
	Comp->SetRelativeLocation(TileOrigin);
	Tile.OriginCm = TileOrigin;
	Tile.LivePos = Positions;
	Tile.StampDir = StampDir;
	Tile.StampSurfM = StampSurfM;
	Tile.LiveN = Normals;
	Tile.UV0 = UV0;
	Tile.Colors = Colors;
	Tile.Tangents = Tangents;
	Tile.Indices = Indices;

	Comp->ClearAllMeshSections();
	if (Positions.Num() >= 3 && Indices.Num() >= 3)
	{
		Comp->CreateMeshSection_LinearColor(0, Positions, Indices, Normals, UV0, Colors, Tangents, true);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetCollisionEnabled(Tile.bHidden ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Comp->SetVisibility(!Tile.bHidden);
		Comp->SetHiddenInGame(false);
		Comp->UpdateBounds();
	}
	GX_PERF(1, TEXT("GX-tile face=%d u=%d v=%d verts=%d tris=%d"),
		Tile.Key.Face, Tile.Key.U, Tile.Key.V, Positions.Num(), Indices.Num() / 3);
}

void FGXCrustTiles::Update(
	AActor* Owner,
	const FGXSphereStamp& Stamp,
	const FVector& ViewerLocalM,
	UMaterialInterface* Material,
	int32 MaxBuildsThisTick,
	TFunction<float(const FVector&)> DensityAt)
{
	if (!Owner)
	{
		return;
	}
	const FGXCrustTileKey Center = KeyAt(ViewerLocalM, 0);
	const int32 Reach = FMath::CeilToInt(StreamM / TileM);
	TSet<FGXCrustTileKey> Desired;
	for (int32 DV = -Reach; DV <= Reach; ++DV)
	{
		for (int32 DU = -Reach; DU <= Reach; ++DU)
		{
			FGXCrustTileKey K = Center;
			K.U += DU;
			K.V += DV;
			const float CU = (static_cast<float>(K.U) + 0.5f) * TileM;
			const float CV = (static_cast<float>(K.V) + 0.5f) * TileM;
			FVector N, T, B;
			FaceAxes(K.Face, N, T, B);
			const FVector Approx = N * Stamp.GetParams().Radius + T * CU + B * CV;
			if (HiddenKeys.Contains(K))
			{
				continue;
			}
			if (FVector::DistSquared(Approx, ViewerLocalM) <= FMath::Square(StreamM + TileM))
			{
				Desired.Add(K);
			}
		}
	}

	TArray<FGXCrustTileKey> Evict;
	for (const auto& Pair : Live)
	{
		if (!Desired.Contains(Pair.Key))
		{
			Evict.Add(Pair.Key);
		}
	}
	for (const FGXCrustTileKey& K : Evict)
	{
		if (UProceduralMeshComponent* C = Live[K].Comp.Get())
		{
			C->DestroyComponent();
		}
		Live.Remove(K);
	}

	// Nearest first. TSet order built the far corners (u=-3,v=-3) and
	// Ready fired with no tile under the pawn — 0.9.4 shot 001529 looked
	// through the clipmap hole at the underside of the crust.
	TArray<TPair<float, FGXCrustTileKey>> Queue;
	for (const FGXCrustTileKey& K : Desired)
	{
		if (Live.Contains(K))
		{
			continue;
		}
		const float CU = (static_cast<float>(K.U) + 0.5f) * TileM;
		const float CV = (static_cast<float>(K.V) + 0.5f) * TileM;
		FVector N, T, B;
		FaceAxes(K.Face, N, T, B);
		const FVector Approx = N * Stamp.GetParams().Radius + T * CU + B * CV;
		Queue.Emplace(FVector::DistSquared(Approx, ViewerLocalM), K);
	}
	Queue.Sort([](const TPair<float, FGXCrustTileKey>& A, const TPair<float, FGXCrustTileKey>& B)
	{
		return A.Key < B.Key;
	});

	int32 Built = 0;
	for (const TPair<float, FGXCrustTileKey>& Item : Queue)
	{
		if (Built >= MaxBuildsThisTick)
		{
			break;
		}
		UProceduralMeshComponent* PMC = MakeTilePMC(Owner);
		if (!PMC)
		{
			continue;
		}
		FTile Tile;
		Tile.Key = Item.Value;
		Tile.Comp = PMC;
		BuildTile(Tile, Stamp, Material, DensityAt);
		Live.Add(Item.Value, Tile);
		++Built;
	}
	// Ready only when the pawn's own tile exists — count-only ready was a hole.
	bReady = Live.Contains(Center) && Live.Num() >= ReadyMin;
	if (Built > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles live=%d built=%d ready=%d face=%d u=%d v=%d"),
			Live.Num(), Built, bReady ? 1 : 0, Center.Face, Center.U, Center.V);
	}
}
