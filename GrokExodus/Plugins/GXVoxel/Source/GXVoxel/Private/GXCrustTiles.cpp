// Copyright Grok Exodus. All Rights Reserved.

#include "GXCrustTiles.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarGXNaniteTiles(
	TEXT("gx.nanite.tiles"),
	0,
	TEXT("Walk tiles: 0=PMC (default). 1=Nanite underfoot only — that cracked the Y=0 tile edge."),
	ECVF_Default);
#endif

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

#if WITH_EDITOR
	UStaticMeshComponent* MakeNaniteSMC(AActor* Owner)
	{
		UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		if (!SMC)
		{
			return nullptr;
		}
		SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SMC->SetCastShadow(false);
		SMC->SetVisibleInRayTracing(false);
		SMC->bNeverDistanceCull = true;
		SMC->SetCullDistance(0.0f);
		SMC->SetBoundsScale(4.0f);
		SMC->SetupAttachment(Owner->GetRootComponent());
		SMC->RegisterComponent();
		return SMC;
	}
#endif
}

void FGXCrustTiles::Initialize(AActor* Owner)
{
	Shutdown();
	OwnerCached = Owner;
	bReady = false;
	LastNaniteCookSeconds = -1.0e9;
	ReadyAtSeconds = -1.0e9;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles: tile=%.0f cell=%.2f fine=%.2f stream=%.0f (0.10.6 cap=R, no overlap)"),
		TileM, CellM, FineCellM, StreamM);
}

void FGXCrustTiles::Shutdown()
{
	for (auto& Pair : Live)
	{
		DestroyTileVisuals(Pair.Value);
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
		DestroyTileVisuals(*T);
		Live.Remove(Key);
	}
}

int32 FGXCrustTiles::HideTilesInSphere(const FVector& LocalM, float RadiusM)
{
	const float Pad = FMath::Max(RadiusM, 0.5f) + 2.0f;
	const FGXCrustTileKey Center = KeyAt(LocalM, 0);
	int32 N = 0;
	auto HideIfNear = [&](const FGXCrustTileKey& K)
	{
		if (HiddenKeys.Contains(K) && !Live.Contains(K))
		{
			return;
		}
		FVector Nrm, T, B;
		FaceAxes(K.Face, Nrm, T, B);
		const float Scale = TileM * static_cast<float>(1 << FMath::Max(0, K.LOD));
		const float U0 = static_cast<float>(K.U) * Scale;
		const float V0 = static_cast<float>(K.V) * Scale;
		const float U = FVector::DotProduct(LocalM, T);
		const float V = FVector::DotProduct(LocalM, B);
		const float Du = FMath::Max(0.0f, FMath::Max(U0 - U, U - (U0 + Scale)));
		const float Dv = FMath::Max(0.0f, FMath::Max(V0 - V, V - (V0 + Scale)));
		if ((Du * Du + Dv * Dv) > (Pad * Pad))
		{
			return;
		}
		HideTile(K);
		++N;
	};
	HideIfNear(Center);
	for (int32 DV = -1; DV <= 1; ++DV)
	{
		for (int32 DU = -1; DU <= 1; ++DU)
		{
			if (DU == 0 && DV == 0)
			{
				continue;
			}
			FGXCrustTileKey K = Center;
			K.U += DU;
			K.V += DV;
			HideIfNear(K);
		}
	}
	if (N > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles hide=%d at (%.0f,%.0f,%.0f) r=%.1f"),
			N, LocalM.X, LocalM.Y, LocalM.Z, RadiusM);
	}
	return N;
}

int32 FGXCrustTiles::NotifyBrush(
	const FVector& LocalM,
	float RadiusM,
	bool bRemove,
	const FGXSphereStamp& Stamp,
	UMaterialInterface* Material,
	const TFunction<float(const FVector&)>& DensityAt,
	int32* OutPunched,
	bool bAllowPunch,
	bool* OutSteep)
{
	if (OutPunched)
	{
		*OutPunched = 0;
	}
	if (OutSteep)
	{
		*OutSteep = false;
	}
	if (RadiusM <= 0.0f || Live.Num() == 0)
	{
		return 0;
	}
	(void)DensityAt;
	const FVector BrushDir = LocalM.GetSafeNormal();
	if (BrushDir.IsNearlyZero())
	{
		return 0;
	}
	// Influence is the brush sphere plus one cell of rim. 1.45 R + 2 cell
	// plus FlattenLongQuads was the GX-pool-0120 swimming-pool shaft.
	const float Cover = RadiusM + FineCellM + 0.25f;
	const float Cover2 = Cover * Cover;
	const float R2 = RadiusM * RadiusM;
	int32 Changed = 0;
	int32 PunchedAll = 0;
	int32 SteepCenter = 0;
	int32 CenterHits = 0;
	// Half-diagonal of a 64 m tile is 45.3 m. 0.55*Tile+4 was 39 m and
	// skipped every tile when the brush sat on a four-tile corner (spawn).
	const float TileReach2 = FMath::Square(Cover + TileM * 0.80f + 8.0f);
	// At most one FineCell cook per stroke. Cooking every nearby 16 k-vert
	// tile (0.10.14) took longer than the 140 ms hold tick — later digs
	// never ran their visual drop.
	{
		FGXCrustTileKey BestKey;
		float BestDs = TileReach2;
		bool bNeed = false;
		const FGXCrustTileKey Prefer = KeyAt(LocalM, 0);
		if (FTile* Pref = Live.Find(Prefer))
		{
			if (FMath::Abs(Pref->FineCell - FineCellM) > 0.01f)
			{
				BestKey = Prefer;
				bNeed = true;
			}
		}
		if (!bNeed)
		{
			for (auto& Pair : Live)
			{
				if (FMath::Abs(Pair.Value.FineCell - FineCellM) <= 0.01f)
				{
					continue;
				}
				const float Ds = FVector::DistSquared(Pair.Value.OriginCm * 0.01f, LocalM);
				if (Ds < BestDs)
				{
					BestDs = Ds;
					BestKey = Pair.Key;
					bNeed = true;
				}
			}
		}
		if (bNeed)
		{
			if (FTile* T = Live.Find(BestKey))
			{
				T->FineCell = FineCellM;
				BuildTile(*T, Stamp, Material, nullptr);
			}
		}
	}
	for (auto& Pair : Live)
	{
		FTile& Tile = Pair.Value;
		const FVector TileWorld = Tile.OriginCm * 0.01f;
		if (FVector::DistSquared(TileWorld, LocalM) > TileReach2)
		{
			continue;
		}
		UProceduralMeshComponent* Comp = Tile.Comp.Get();
		if (!Comp || Tile.LivePos.Num() == 0 || Tile.StampDir.Num() != Tile.LivePos.Num())
		{
			continue;
		}
		DropNanite(Tile);
		Tile.bSculpted = true;

		if (bRemove)
		{
			// Snap verts to the brush sphere. A wide cover + min-R flatten
			// cut a rectangular pool (GX-pool-0120). Rim is one cell only.
			int32 N = 0;
			const int32 Dim = GridDim(Tile);
			const float Cell = (Tile.FineCell > 0.1f) ? Tile.FineCell : CellM;
			const float Rim = Cell;
			const float MaxStep = RadiusM * 2.0f;
			FVector FaceN, AxisT, AxisB;
			FaceAxes(Tile.Key.Face, FaceN, AxisT, AxisB);
			const float OriginU = static_cast<float>(Tile.Key.U) * TileM;
			const float OriginV = static_cast<float>(Tile.Key.V) * TileM;
			const int32 IU = FMath::RoundToInt((FVector::DotProduct(LocalM, AxisT) - OriginU) / Cell);
			const int32 IV = FMath::RoundToInt((FVector::DotProduct(LocalM, AxisB) - OriginV) / Cell);
			const int32 Reach = (Dim >= 2)
				? FMath::Clamp(FMath::CeilToInt(Cover / Cell) + 2, 2, Dim)
				: 0;
			auto SculptVert = [&](int32 Idx)
			{
				if (!Tile.LivePos.IsValidIndex(Idx) || !Tile.StampDir.IsValidIndex(Idx))
				{
					return;
				}
				const FVector Dir = Tile.StampDir[Idx];
				const FVector W = (Tile.OriginCm + Tile.LivePos[Idx]) * 0.01f;
				const float Dist3 = FVector::Dist(W, LocalM);
				if (Dist3 > Cover)
				{
					return;
				}
				const float CurR = W.Size();
				const float Bcoe = FVector::DotProduct(Dir, LocalM);
				const float Disc = Bcoe * Bcoe - (LocalM.SizeSquared() - R2);
				if (Disc < 0.0f)
				{
					return;
				}
				const float THit = Bcoe - FMath::Sqrt(Disc);
				if (THit <= 0.0f || THit >= CurR)
				{
					return;
				}
				float TargetR = FMath::Max(THit, CurR - MaxStep);
				float Wgt = 1.0f;
				if (Dist3 > RadiusM)
				{
					Wgt = 1.0f - (Dist3 - RadiusM) / FMath::Max(Rim, 0.05f);
					Wgt = FMath::Clamp(Wgt, 0.0f, 1.0f);
					Wgt = Wgt * Wgt * (3.0f - 2.0f * Wgt);
				}
				const float NewR = FMath::Lerp(CurR, TargetR, Wgt);
				if (FMath::Abs(NewR - CurR) < 0.01f)
				{
					return;
				}
				Tile.LivePos[Idx] = Dir * NewR * 100.0f - Tile.OriginCm;
				if (Dist3 <= RadiusM && Tile.UV0.IsValidIndex(Idx))
				{
					Tile.UV0[Idx] = FVector2D(3.0f, 0.0f);
					if (Tile.Colors.IsValidIndex(Idx))
					{
						Tile.Colors[Idx] = FLinearColor(0.58f, 0.50f, 0.44f, 1.0f);
					}
				}
				++N;
			};
			int32 I0 = 0, I1 = 0, J0 = 0, J1 = 0;
			if (Dim >= 2 && Reach > 0)
			{
				I0 = FMath::Clamp(IU - Reach, 0, Dim - 1);
				I1 = FMath::Clamp(IU + Reach, 0, Dim - 1);
				J0 = FMath::Clamp(IV - Reach, 0, Dim - 1);
				J1 = FMath::Clamp(IV + Reach, 0, Dim - 1);
				for (int32 J = J0; J <= J1; ++J)
				{
					for (int32 I = I0; I <= I1; ++I)
					{
						SculptVert(I + J * Dim);
					}
				}
			}
			else
			{
				for (int32 I = 0; I < Tile.LivePos.Num(); ++I)
				{
					SculptVert(I);
				}
			}
			if (N == 0)
			{
				continue;
			}
			if (Dim >= 2)
			{
				RecomputeNormalsWindow(Tile, I0, I1, J0, J1);
			}
			else
			{
				RecomputeNormals(Tile);
			}
			const float CenterR = RadiusM * 0.65f;
			const float CenterR2 = CenterR * CenterR;
			if (Dim >= 2)
			{
				for (int32 J = J0; J <= J1; ++J)
				{
					for (int32 I = I0; I <= I1; ++I)
					{
						const int32 Idx = I + J * Dim;
						if (!Tile.LivePos.IsValidIndex(Idx) || !Tile.LiveN.IsValidIndex(Idx))
						{
							continue;
						}
						const FVector W = (Tile.OriginCm + Tile.LivePos[Idx]) * 0.01f;
						if (FVector::DistSquared(W, LocalM) > CenterR2)
						{
							continue;
						}
						++CenterHits;
						const FVector Rad = Tile.StampDir.IsValidIndex(Idx)
							? Tile.StampDir[Idx]
							: W.GetSafeNormal();
						if (FMath::Abs(FVector::DotProduct(Tile.LiveN[Idx], Rad)) < 0.58f)
						{
							++SteepCenter;
						}
					}
				}
			}
			int32 PunchedHere = 0;
			(void)bAllowPunch;
			if (PunchedHere > 0)
			{
				Comp->ClearMeshSection(0);
				Comp->CreateMeshSection_LinearColor(
					0, Tile.LivePos, Tile.Indices, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents, true);
				if (Material)
				{
					Comp->SetMaterial(0, Material);
				}
			}
			else
			{
				Comp->UpdateMeshSection_LinearColor(
					0, Tile.LivePos, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents);
			}
			Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Comp->SetVisibility(true);
			Comp->UpdateBounds();
			Changed += N;
			continue;
		}

		int32 N = 0;
		for (int32 I = 0; I < Tile.LivePos.Num(); ++I)
		{
			const FVector Dir = Tile.StampDir[I];
			const FVector W = (Tile.OriginCm + Tile.LivePos[I]) * 0.01f;
			if (FVector::DistSquared(W, LocalM) > Cover2)
			{
				continue;
			}
			const float CurR = W.Size();
			const float Bcoe = FVector::DotProduct(Dir, LocalM);
			const float Disc = Bcoe * Bcoe - (LocalM.SizeSquared() - R2);
			const float MaxStep = RadiusM * 0.90f;
			float NewR = CurR;
			if (Disc >= 0.0f)
			{
				const float Root = FMath::Sqrt(Disc);
				const float THit = Bcoe + Root;
				NewR = FMath::Min(THit, CurR + MaxStep * 0.55f);
				NewR = FMath::Max(CurR, NewR);
			}
			else
			{
				const float D3 = FVector::Dist(W, LocalM);
				float Wgt = 1.0f - D3 / Cover;
				Wgt = Wgt * Wgt * (3.0f - 2.0f * Wgt);
				NewR = CurR + MaxStep * 0.25f * Wgt;
			}
			if (FMath::Abs(NewR - CurR) < 0.01f)
			{
				continue;
			}
			Tile.LivePos[I] = Dir * NewR * 100.0f - Tile.OriginCm;
			++N;
		}
		if (N == 0)
		{
			continue;
		}
		RecomputeNormals(Tile);
		Comp->UpdateMeshSection_LinearColor(
			0, Tile.LivePos, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetVisibility(true);
		Comp->UpdateBounds();
		Changed += N;
	}
	if (bRemove && Changed > 0)
	{
		WeldSeamsNear(LocalM, Cover, Material, Changed);
	}
	if (OutSteep && CenterHits > 0 && SteepCenter * 2 > CenterHits)
	{
		*OutSteep = true;
	}
	if (Changed > 0 || PunchedAll > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles sculpt %s n=%d punch=%d steep=%d/%d r=%.2f cell=%.2f"),
			bRemove ? TEXT("dig") : TEXT("place"), Changed, PunchedAll, SteepCenter, CenterHits, RadiusM, FineCellM);
		GX_PERF(1, TEXT("GX-tile sculpt %s n=%d punch=%d steep=%d/%d r=%.2f"),
			bRemove ? TEXT("dig") : TEXT("place"), Changed, PunchedAll, SteepCenter, CenterHits, RadiusM);
	}
	if (OutPunched)
	{
		*OutPunched = PunchedAll;
	}
	return Changed;
}

int32 FGXCrustTiles::PunchBrush(
	const FVector& LocalM,
	float RadiusM,
	UMaterialInterface* Material,
	const TFunction<bool(const FVector&)>& CaveCovers)
{
	if (RadiusM <= 0.0f || Live.Num() == 0 || !CaveCovers)
	{
		return 0;
	}
	int32 Punched = 0;
	const float TileReach2 = FMath::Square(RadiusM + TileM * 0.80f + 8.0f);
	for (auto& Pair : Live)
	{
		FTile& Tile = Pair.Value;
		if (FVector::DistSquared(Tile.OriginCm * 0.01f, LocalM) > TileReach2)
		{
			continue;
		}
		UProceduralMeshComponent* Comp = Tile.Comp.Get();
		if (!Comp || Tile.LivePos.Num() == 0)
		{
			continue;
		}
		const int32 N = PunchSteepQuads(Tile, LocalM, RadiusM, CaveCovers);
		if (N == 0)
		{
			continue;
		}
		DropNanite(Tile);
		Comp->ClearMeshSection(0);
		Comp->CreateMeshSection_LinearColor(
			0, Tile.LivePos, Tile.Indices, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents, true);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetVisibility(true);
		Comp->UpdateBounds();
		Punched += N;
	}
	if (Punched > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles punch-open n=%d r=%.2f"), Punched, RadiusM);
		GX_PERF(1, TEXT("GX-tile punch-open n=%d r=%.2f"), Punched, RadiusM);
	}
	return Punched;
}

int32 FGXCrustTiles::CloseUncoveredBrush(
	const FVector& LocalM,
	float RadiusM,
	UMaterialInterface* Material,
	const TFunction<bool(const FVector&)>& CaveCovers)
{
	if (RadiusM <= 0.0f || Live.Num() == 0)
	{
		return 0;
	}
	int32 Closed = 0;
	const float TileReach2 = FMath::Square(RadiusM + TileM * 0.80f + 8.0f);
	for (auto& Pair : Live)
	{
		FTile& Tile = Pair.Value;
		if (FVector::DistSquared(Tile.OriginCm * 0.01f, LocalM) > TileReach2)
		{
			continue;
		}
		UProceduralMeshComponent* Comp = Tile.Comp.Get();
		if (!Comp || Tile.LivePos.Num() == 0)
		{
			continue;
		}
		const int32 N = RestoreUncoveredQuads(Tile, LocalM, RadiusM, CaveCovers);
		if (N == 0)
		{
			continue;
		}
		DropNanite(Tile);
		Comp->ClearMeshSection(0);
		Comp->CreateMeshSection_LinearColor(
			0, Tile.LivePos, Tile.Indices, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents, true);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetVisibility(true);
		Comp->UpdateBounds();
		Closed += N;
	}
	if (Closed > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles close-uncovered n=%d r=%.2f"), Closed, RadiusM);
		GX_PERF(1, TEXT("GX-tile close-uncovered n=%d r=%.2f"), Closed, RadiusM);
	}
	return Closed;
}

int32 FGXCrustTiles::GridDim(const FTile& Tile)
{
	const int32 N = Tile.LivePos.Num();
	const int32 D = FMath::RoundToInt(FMath::Sqrt(static_cast<float>(N)));
	return (D > 1 && D * D == N) ? D : 0;
}

void FGXCrustTiles::PushTileMesh(FTile& Tile)
{
	UProceduralMeshComponent* Comp = Tile.Comp.Get();
	if (!Comp || Tile.LivePos.Num() < 3)
	{
		return;
	}
	Comp->UpdateMeshSection_LinearColor(
		0, Tile.LivePos, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents);
	Comp->UpdateBounds();
}

int32 FGXCrustTiles::WeldSharedU(FTile& Left, FTile& Right)
{
	const int32 DL = GridDim(Left);
	const int32 DR = GridDim(Right);
	if (DL < 2 || DL != DR || Left.StampDir.Num() != Left.LivePos.Num()
		|| Right.StampDir.Num() != Right.LivePos.Num())
	{
		return 0;
	}
	int32 N = 0;
	for (int32 J = 0; J < DL; ++J)
	{
		const int32 IL = (DL - 1) + J * DL;
		const int32 IR = 0 + J * DR;
		const FVector WA = (Left.OriginCm + Left.LivePos[IL]) * 0.01f;
		const FVector WB = (Right.OriginCm + Right.LivePos[IR]) * 0.01f;
		const float RA = WA.Size();
		const float RB = WB.Size();
		if (FMath::Abs(RA - RB) < 0.02f)
		{
			continue;
		}
		const float NewR = FMath::Min(RA, RB);
		const FVector Dir = (RA <= RB)
			? (Left.StampDir.IsValidIndex(IL) ? Left.StampDir[IL] : WA.GetSafeNormal())
			: (Right.StampDir.IsValidIndex(IR) ? Right.StampDir[IR] : WB.GetSafeNormal());
		const FVector NewW = Dir * NewR;
		Left.LivePos[IL] = NewW * 100.0f - Left.OriginCm;
		Right.LivePos[IR] = NewW * 100.0f - Right.OriginCm;
		++N;
	}
	return N;
}

int32 FGXCrustTiles::WeldSharedV(FTile& Lo, FTile& Hi)
{
	const int32 DL = GridDim(Lo);
	const int32 DH = GridDim(Hi);
	if (DL < 2 || DL != DH || Lo.StampDir.Num() != Lo.LivePos.Num()
		|| Hi.StampDir.Num() != Hi.LivePos.Num())
	{
		return 0;
	}
	int32 N = 0;
	for (int32 I = 0; I < DL; ++I)
	{
		const int32 IL = I + (DL - 1) * DL;
		const int32 IH = I + 0 * DH;
		const FVector WA = (Lo.OriginCm + Lo.LivePos[IL]) * 0.01f;
		const FVector WB = (Hi.OriginCm + Hi.LivePos[IH]) * 0.01f;
		const float RA = WA.Size();
		const float RB = WB.Size();
		if (FMath::Abs(RA - RB) < 0.02f)
		{
			continue;
		}
		const float NewR = FMath::Min(RA, RB);
		const FVector Dir = (RA <= RB)
			? (Lo.StampDir.IsValidIndex(IL) ? Lo.StampDir[IL] : WA.GetSafeNormal())
			: (Hi.StampDir.IsValidIndex(IH) ? Hi.StampDir[IH] : WB.GetSafeNormal());
		const FVector NewW = Dir * NewR;
		Lo.LivePos[IL] = NewW * 100.0f - Lo.OriginCm;
		Hi.LivePos[IH] = NewW * 100.0f - Hi.OriginCm;
		++N;
	}
	return N;
}

void FGXCrustTiles::WeldSeamsNear(const FVector& LocalM, float CoverM, UMaterialInterface* Material, int32& InOutChanged)
{
	(void)Material;
	const float Reach2 = FMath::Square(CoverM + TileM + 8.0f);
	TArray<FGXCrustTileKey> Keys;
	for (const auto& Pair : Live)
	{
		if (FVector::DistSquared(Pair.Value.OriginCm * 0.01f, LocalM) <= Reach2)
		{
			Keys.Add(Pair.Key);
		}
	}
	TSet<FGXCrustTileKey> Touched;
	for (const FGXCrustTileKey& K : Keys)
	{
		FGXCrustTileKey Right = K;
		Right.U += 1;
		FGXCrustTileKey Up = K;
		Up.V += 1;
		if (FTile* A = Live.Find(K))
		{
			if (FTile* B = Live.Find(Right))
			{
				if (WeldSharedU(*A, *B) > 0)
				{
					Touched.Add(K);
					Touched.Add(Right);
				}
			}
			if (FTile* C = Live.Find(Up))
			{
				if (WeldSharedV(*A, *C) > 0)
				{
					Touched.Add(K);
					Touched.Add(Up);
				}
			}
		}
	}
	for (const FGXCrustTileKey& K : Touched)
	{
		if (FTile* T = Live.Find(K))
		{
			PushTileMesh(*T);
		}
	}
	InOutChanged += Touched.Num();
}

void FGXCrustTiles::RecomputeNormals(FTile& Tile)
{
	const int32 VertN = Tile.LivePos.Num();
	if (VertN == 0 || Tile.Indices.Num() < 3)
	{
		return;
	}
	Tile.LiveN.SetNum(VertN);
	for (int32 I = 0; I < VertN; ++I)
	{
		Tile.LiveN[I] = FVector::ZeroVector;
	}
	for (int32 T = 0; T + 2 < Tile.Indices.Num(); T += 3)
	{
		const int32 IA = Tile.Indices[T];
		const int32 IB = Tile.Indices[T + 1];
		const int32 IC = Tile.Indices[T + 2];
		if (!Tile.LivePos.IsValidIndex(IA) || !Tile.LivePos.IsValidIndex(IB) || !Tile.LivePos.IsValidIndex(IC))
		{
			continue;
		}
		const FVector& PA = Tile.LivePos[IA];
		const FVector& PB = Tile.LivePos[IB];
		const FVector& PC = Tile.LivePos[IC];
		FVector FaceN = FVector::CrossProduct(PB - PA, PC - PA);
		if (FaceN.IsNearlyZero())
		{
			continue;
		}
		const FVector Mid = (PA + PB + PC) * (1.0f / 3.0f) + Tile.OriginCm;
		if (FVector::DotProduct(FaceN, Mid) < 0.0f)
		{
			FaceN = -FaceN;
		}
		FaceN.Normalize();
		Tile.LiveN[IA] += FaceN;
		Tile.LiveN[IB] += FaceN;
		Tile.LiveN[IC] += FaceN;
	}
	if (Tile.Tangents.Num() != VertN)
	{
		Tile.Tangents.SetNum(VertN);
	}
	for (int32 I = 0; I < VertN; ++I)
	{
		if (!Tile.LiveN[I].Normalize())
		{
			Tile.LiveN[I] = Tile.StampDir.IsValidIndex(I) ? Tile.StampDir[I] : FVector::UpVector;
		}
		FVector Tan = FVector::CrossProduct(Tile.LiveN[I], FVector::ZAxisVector);
		if (Tan.SizeSquared() < 1e-6f)
		{
			Tan = FVector::CrossProduct(Tile.LiveN[I], FVector::YAxisVector);
		}
		Tan.Normalize();
		Tile.Tangents[I] = FProcMeshTangent(Tan, false);
	}
}

void FGXCrustTiles::RecomputeNormalsWindow(FTile& Tile, int32 I0, int32 I1, int32 J0, int32 J1)
{
	const int32 Dim = GridDim(Tile);
	if (Dim < 2 || Tile.LivePos.Num() != Dim * Dim)
	{
		RecomputeNormals(Tile);
		return;
	}
	if (Tile.LiveN.Num() != Tile.LivePos.Num())
	{
		Tile.LiveN.SetNum(Tile.LivePos.Num());
	}
	if (Tile.Tangents.Num() != Tile.LivePos.Num())
	{
		Tile.Tangents.SetNum(Tile.LivePos.Num());
	}
	I0 = FMath::Clamp(I0 - 1, 0, Dim - 1);
	I1 = FMath::Clamp(I1 + 1, 0, Dim - 1);
	J0 = FMath::Clamp(J0 - 1, 0, Dim - 1);
	J1 = FMath::Clamp(J1 + 1, 0, Dim - 1);
	for (int32 J = J0; J <= J1; ++J)
	{
		for (int32 I = I0; I <= I1; ++I)
		{
			Tile.LiveN[I + J * Dim] = FVector::ZeroVector;
		}
	}
	const int32 Cells = Dim - 1;
	const bool bMask = Tile.QuadAlive.Num() == Cells * Cells;
	const int32 CI0 = FMath::Clamp(I0, 0, Cells - 1);
	const int32 CI1 = FMath::Clamp(I1, 0, Cells - 1);
	const int32 CJ0 = FMath::Clamp(J0, 0, Cells - 1);
	const int32 CJ1 = FMath::Clamp(J1, 0, Cells - 1);
	auto AddFace = [&](int32 IA, int32 IB, int32 IC)
	{
		if (!Tile.LivePos.IsValidIndex(IA) || !Tile.LivePos.IsValidIndex(IB) || !Tile.LivePos.IsValidIndex(IC))
		{
			return;
		}
		const FVector& PA = Tile.LivePos[IA];
		const FVector& PB = Tile.LivePos[IB];
		const FVector& PC = Tile.LivePos[IC];
		FVector FaceN = FVector::CrossProduct(PB - PA, PC - PA);
		if (FaceN.IsNearlyZero())
		{
			return;
		}
		const FVector Mid = (PA + PB + PC) * (1.0f / 3.0f) + Tile.OriginCm;
		if (FVector::DotProduct(FaceN, Mid) < 0.0f)
		{
			FaceN = -FaceN;
		}
		FaceN.Normalize();
		Tile.LiveN[IA] += FaceN;
		Tile.LiveN[IB] += FaceN;
		Tile.LiveN[IC] += FaceN;
	};
	for (int32 J = CJ0; J <= CJ1; ++J)
	{
		for (int32 I = CI0; I <= CI1; ++I)
		{
			if (bMask && !Tile.QuadAlive[I + J * Cells])
			{
				continue;
			}
			const int32 A = I + J * Dim;
			const int32 Bv = (I + 1) + J * Dim;
			const int32 C = I + (J + 1) * Dim;
			const int32 D = (I + 1) + (J + 1) * Dim;
			AddFace(A, C, Bv);
			AddFace(Bv, C, D);
		}
	}
	for (int32 J = J0; J <= J1; ++J)
	{
		for (int32 I = I0; I <= I1; ++I)
		{
			const int32 Idx = I + J * Dim;
			if (!Tile.LiveN[Idx].Normalize())
			{
				Tile.LiveN[Idx] = Tile.StampDir.IsValidIndex(Idx) ? Tile.StampDir[Idx] : FVector::UpVector;
			}
			FVector Tan = FVector::CrossProduct(Tile.LiveN[Idx], FVector::ZAxisVector);
			if (Tan.SizeSquared() < 1e-6f)
			{
				Tan = FVector::CrossProduct(Tile.LiveN[Idx], FVector::YAxisVector);
			}
			Tan.Normalize();
			Tile.Tangents[Idx] = FProcMeshTangent(Tan, false);
		}
	}
}

void FGXCrustTiles::RebuildIndices(FTile& Tile)
{
	const int32 Dim = GridDim(Tile);
	if (Dim < 2)
	{
		return;
	}
	const int32 Cells = Dim - 1;
	const bool bMask = Tile.QuadAlive.Num() == Cells * Cells;
	Tile.Indices.Reset();
	Tile.Indices.Reserve(Cells * Cells * 6);
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			if (bMask && !Tile.QuadAlive[I + J * Cells])
			{
				continue;
			}
			const int32 A = I + J * Dim;
			const int32 Bv = (I + 1) + J * Dim;
			const int32 C = I + (J + 1) * Dim;
			const int32 D = (I + 1) + (J + 1) * Dim;
			Tile.Indices.Add(A);
			Tile.Indices.Add(C);
			Tile.Indices.Add(Bv);
			Tile.Indices.Add(Bv);
			Tile.Indices.Add(C);
			Tile.Indices.Add(D);
		}
	}
}

int32 FGXCrustTiles::PunchSteepQuads(
	FTile& Tile,
	const FVector& LocalM,
	float RadiusM,
	const TFunction<bool(const FVector&)>& CaveCovers)
{
	const int32 Dim = GridDim(Tile);
	if (Dim < 2 || RadiusM <= 0.0f || Tile.LivePos.Num() != Dim * Dim || !CaveCovers)
	{
		return 0;
	}
	const int32 Cells = Dim - 1;
	if (Tile.QuadAlive.Num() != Cells * Cells)
	{
		Tile.QuadAlive.Init(true, Cells * Cells);
	}
	const float R2 = RadiusM * RadiusM;
	int32 N = 0;
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			const int32 Q = I + J * Cells;
			if (!Tile.QuadAlive[Q])
			{
				continue;
			}
			const int32 A = I + J * Dim;
			const int32 Bv = (I + 1) + J * Dim;
			const int32 C = I + (J + 1) * Dim;
			const int32 D = (I + 1) + (J + 1) * Dim;
			const FVector WA = (Tile.OriginCm + Tile.LivePos[A]) * 0.01f;
			const FVector WB = (Tile.OriginCm + Tile.LivePos[Bv]) * 0.01f;
			const FVector WC = (Tile.OriginCm + Tile.LivePos[C]) * 0.01f;
			const FVector WD = (Tile.OriginCm + Tile.LivePos[D]) * 0.01f;
			const FVector Cent = (WA + WB + WC + WD) * 0.25f;
			if (FVector::DistSquared(Cent, LocalM) > R2)
			{
				continue;
			}
			FVector FaceN = FVector::CrossProduct(WC - WA, WB - WA);
			if (!FaceN.Normalize())
			{
				continue;
			}
			const FVector Rad = Cent.GetSafeNormal();
			// Floor / lid stays closed. Only a wall mouth can open.
			if (FMath::Abs(FVector::DotProduct(FaceN, Rad)) > 0.62f)
			{
				continue;
			}
			// 0.10.18 punched any steep quad once a cave chunk existed —
			// 17 floor tris cannot fill a wall window (GX-holes-0118).
			if (!CaveCovers(Cent))
			{
				continue;
			}
			Tile.QuadAlive[Q] = false;
			++N;
		}
	}
	if (N == 0)
	{
		return 0;
	}
	RebuildIndices(Tile);
	if (Tile.Indices.Num() < 3)
	{
		Tile.QuadAlive.Init(true, Cells * Cells);
		RebuildIndices(Tile);
		return 0;
	}
	return N;
}

int32 FGXCrustTiles::RestoreUncoveredQuads(
	FTile& Tile,
	const FVector& LocalM,
	float RadiusM,
	const TFunction<bool(const FVector&)>& CaveCovers)
{
	const int32 Dim = GridDim(Tile);
	if (Dim < 2 || Tile.LivePos.Num() != Dim * Dim)
	{
		return 0;
	}
	const int32 Cells = Dim - 1;
	if (Tile.QuadAlive.Num() != Cells * Cells)
	{
		return 0;
	}
	const float R2 = FMath::Square(FMath::Max(RadiusM, 1.0f));
	int32 N = 0;
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			const int32 Q = I + J * Cells;
			if (Tile.QuadAlive[Q])
			{
				continue;
			}
			const int32 A = I + J * Dim;
			const int32 Bv = (I + 1) + J * Dim;
			const int32 C = I + (J + 1) * Dim;
			const int32 D = (I + 1) + (J + 1) * Dim;
			const FVector WA = (Tile.OriginCm + Tile.LivePos[A]) * 0.01f;
			const FVector WB = (Tile.OriginCm + Tile.LivePos[Bv]) * 0.01f;
			const FVector WC = (Tile.OriginCm + Tile.LivePos[C]) * 0.01f;
			const FVector WD = (Tile.OriginCm + Tile.LivePos[D]) * 0.01f;
			const FVector Cent = (WA + WB + WC + WD) * 0.25f;
			if (FVector::DistSquared(Cent, LocalM) > R2)
			{
				continue;
			}
			if (CaveCovers && CaveCovers(Cent))
			{
				continue;
			}
			Tile.QuadAlive[Q] = true;
			++N;
		}
	}
	if (N > 0)
	{
		RebuildIndices(Tile);
	}
	return N;
}

void FGXCrustTiles::DropNanite(FTile& Tile)
{
	if (UStaticMeshComponent* SMC = Tile.NaniteComp.Get())
	{
		SMC->DestroyComponent();
	}
	Tile.NaniteComp.Reset();
	Tile.NaniteMesh.Reset();
	if (UProceduralMeshComponent* PMC = Tile.Comp.Get())
	{
		PMC->SetVisibility(!Tile.bHidden);
	}
}

void FGXCrustTiles::DestroyTileVisuals(FTile& Tile)
{
	DropNanite(Tile);
	if (UProceduralMeshComponent* C = Tile.Comp.Get())
	{
		C->DestroyComponent();
	}
	Tile.Comp.Reset();
}

void FGXCrustTiles::ApplyNaniteVisual(FTile& Tile, UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (CVarGXNaniteTiles.GetValueOnGameThread() <= 0)
	{
		if (UStaticMeshComponent* Old = Tile.NaniteComp.Get())
		{
			Old->DestroyComponent();
		}
		Tile.NaniteComp.Reset();
		Tile.NaniteMesh.Reset();
		if (UProceduralMeshComponent* PMC = Tile.Comp.Get())
		{
			PMC->SetVisibility(!Tile.bHidden);
		}
		return;
	}
	if (Tile.LivePos.Num() < 3 || Tile.Indices.Num() < 3 || !OwnerCached)
	{
		return;
	}

	const double T0 = FPlatformTime::Seconds();
	if (!Tile.NaniteMesh.IsValid())
	{
		UStaticMesh* SM = NewObject<UStaticMesh>(OwnerCached, NAME_None, RF_Transient | RF_DuplicateTransient);
		if (!SM)
		{
			return;
		}
		SM->bAllowCPUAccess = false;
		SM->NeverStream = true;
		Tile.NaniteMesh.Reset(SM);
	}
	UStaticMesh* SM = Tile.NaniteMesh.Get();

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> InstN = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> InstT = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> InstB = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> InstUV = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> InstC = Attr.GetVertexInstanceColors();
	InstUV.SetNumChannels(1);

	const int32 NV = Tile.LivePos.Num();
	TArray<FVertexID> Verts;
	Verts.SetNum(NV);
	for (int32 I = 0; I < NV; ++I)
	{
		Verts[I] = MD.CreateVertex();
		Positions[Verts[I]] = FVector3f(Tile.LivePos[I]);
	}

	const FPolygonGroupID PG = MD.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[PG] = FName(TEXT("Tile"));

	for (int32 Tri = 0; Tri + 2 < Tile.Indices.Num(); Tri += 3)
	{
		const int32 Src[3] = { Tile.Indices[Tri], Tile.Indices[Tri + 1], Tile.Indices[Tri + 2] };
		if (!Verts.IsValidIndex(Src[0]) || !Verts.IsValidIndex(Src[1]) || !Verts.IsValidIndex(Src[2]))
		{
			continue;
		}
		FVertexInstanceID II[3];
		for (int32 K = 0; K < 3; ++K)
		{
			II[K] = MD.CreateVertexInstance(Verts[Src[K]]);
			InstN[II[K]] = FVector3f(Tile.LiveN.IsValidIndex(Src[K]) ? Tile.LiveN[Src[K]] : FVector::UpVector);
			const FVector Tan = Tile.Tangents.IsValidIndex(Src[K]) ? FVector(Tile.Tangents[Src[K]].TangentX) : FVector::ForwardVector;
			InstT[II[K]] = FVector3f(Tan.GetSafeNormal());
			InstB[II[K]] = 1.0f;
			const FVector2D UV = Tile.UV0.IsValidIndex(Src[K]) ? Tile.UV0[Src[K]] : FVector2D(1.0f, 0.0f);
			InstUV.Set(II[K], 0, FVector2f(static_cast<float>(UV.X), static_cast<float>(UV.Y)));
			const FLinearColor Col = Tile.Colors.IsValidIndex(Src[K]) ? Tile.Colors[Src[K]] : FLinearColor::White;
			InstC[II[K]] = FVector4f(Col);
		}
		const TArray<FVertexInstanceID> Loop = { II[0], II[1], II[2] };
		MD.CreateTriangle(PG, Loop);
	}

	FMeshNaniteSettings Nanite = SM->GetNaniteSettings();
	Nanite.bEnabled = true;
	Nanite.bLerpUVs = false;
	Nanite.KeepPercentTriangles = 1.0f;
	SM->SetNaniteSettings(Nanite);

	if (Material)
	{
		SM->SetStaticMaterials({ FStaticMaterial(Material) });
	}

	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = false;
	Params.bBuildSimpleCollision = false;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = true;
	if (!SM->BuildFromMeshDescriptions(Descs, Params))
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles Nanite build failed face=%d u=%d v=%d"),
			Tile.Key.Face, Tile.Key.U, Tile.Key.V);
		return;
	}

	UStaticMeshComponent* SMC = Tile.NaniteComp.Get();
	if (!SMC)
	{
		SMC = MakeNaniteSMC(OwnerCached);
		if (!SMC)
		{
			return;
		}
		Tile.NaniteComp = SMC;
	}
	SMC->SetStaticMesh(SM);
	if (Material)
	{
		SMC->SetMaterial(0, Material);
	}
	SMC->SetRelativeLocation(Tile.OriginCm);
	SMC->SetVisibility(!Tile.bHidden);
	SMC->UpdateBounds();

	if (UProceduralMeshComponent* PMC = Tile.Comp.Get())
	{
		// Collision stays on the PMC. Nanite is the visible crust.
		PMC->SetVisibility(false);
	}

	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	GX_PERF(1, TEXT("GX-nanite face=%d u=%d v=%d verts=%d ms=%.1f"),
		Tile.Key.Face, Tile.Key.U, Tile.Key.V, NV, Ms);
#else
	(void)Tile;
	(void)Material;
#endif
}

bool FGXCrustTiles::HasTileAt(const FVector& LocalM) const
{
	return Live.Contains(KeyAt(LocalM, 0));
}

bool FGXCrustTiles::RaycastVisible(
	const FVector& WorldOriginCm,
	const FVector& WorldDir,
	float MaxCm,
	FVector& OutHitCm,
	FVector& OutNormal) const
{
	const FVector Dir = WorldDir.GetSafeNormal();
	if (Dir.IsNearlyZero() || MaxCm <= 0.0f || Live.Num() == 0)
	{
		return false;
	}
	auto RayTri = [](const FVector& Orig, const FVector& D, const FVector& A, const FVector& B, const FVector& C,
		float MaxT, float& OutT, FVector& OutN) -> bool
	{
		const FVector E1 = B - A;
		const FVector E2 = C - A;
		const FVector P = FVector::CrossProduct(D, E2);
		const float Det = FVector::DotProduct(E1, P);
		if (FMath::Abs(Det) < 1.0e-10f)
		{
			return false;
		}
		const float Inv = 1.0f / Det;
		const FVector TV = Orig - A;
		const float U = FVector::DotProduct(TV, P) * Inv;
		if (U < 0.0f || U > 1.0f)
		{
			return false;
		}
		const FVector Q = FVector::CrossProduct(TV, E1);
		const float V = FVector::DotProduct(D, Q) * Inv;
		if (V < 0.0f || U + V > 1.0f)
		{
			return false;
		}
		const float T = FVector::DotProduct(E2, Q) * Inv;
		if (T < 1.0f || T > MaxT)
		{
			return false;
		}
		OutT = T;
		OutN = FVector::CrossProduct(E1, E2);
		if (!OutN.Normalize())
		{
			return false;
		}
		if (FVector::DotProduct(OutN, D) > 0.0f)
		{
			OutN = -OutN;
		}
		return true;
	};

	float BestT = MaxCm;
	FVector BestN = FVector::ZeroVector;
	bool bHit = false;
	const FVector ActorLoc = OwnerCached ? OwnerCached->GetActorLocation() : FVector::ZeroVector;
	const float StepCm = 35.0f;
	for (float Ts = 0.0f; Ts <= MaxCm; Ts += StepCm)
	{
		const FVector WorldP = WorldOriginCm + Dir * Ts;
		const FVector LocalM = (WorldP - ActorLoc) * 0.01f;
		const FTile* Tile = Live.Find(KeyAt(LocalM, 0));
		if (!Tile)
		{
			continue;
		}
		UProceduralMeshComponent* Comp = Tile->Comp.Get();
		const int32 Dim = GridDim(*Tile);
		if (!Comp || Dim < 2 || Tile->LivePos.Num() != Dim * Dim)
		{
			continue;
		}
		const float Cell = (Tile->FineCell > 0.1f) ? Tile->FineCell : CellM;
		FVector FaceN, AxisT, AxisB;
		FaceAxes(Tile->Key.Face, FaceN, AxisT, AxisB);
		const float OriginU = static_cast<float>(Tile->Key.U) * TileM;
		const float OriginV = static_cast<float>(Tile->Key.V) * TileM;
		const int32 IU = FMath::FloorToInt((FVector::DotProduct(LocalM, AxisT) - OriginU) / Cell);
		const int32 IV = FMath::FloorToInt((FVector::DotProduct(LocalM, AxisB) - OriginV) / Cell);
		const FTransform Xf = Comp->GetComponentTransform();
		const FVector LO = Xf.InverseTransformPosition(WorldOriginCm);
		FVector LD = Xf.InverseTransformVectorNoScale(Dir);
		if (!LD.Normalize())
		{
			continue;
		}
		auto Vert = [&](int32 I, int32 J) -> FVector
		{
			return Tile->LivePos[FMath::Clamp(I, 0, Dim - 1) + FMath::Clamp(J, 0, Dim - 1) * Dim];
		};
		const int32 Cells = Dim - 1;
		const bool bMask = Tile->QuadAlive.Num() == Cells * Cells;
		for (int32 J = FMath::Max(0, IV - 1); J <= FMath::Min(Dim - 2, IV + 1); ++J)
		{
			for (int32 I = FMath::Max(0, IU - 1); I <= FMath::Min(Dim - 2, IU + 1); ++I)
			{
				if (bMask && !Tile->QuadAlive[I + J * Cells])
				{
					continue;
				}
				const FVector A = Vert(I, J);
				const FVector Bv = Vert(I + 1, J);
				const FVector C = Vert(I, J + 1);
				const FVector D = Vert(I + 1, J + 1);
				float T = 0.0f;
				FVector N = FVector::ZeroVector;
				if (RayTri(LO, LD, A, C, Bv, BestT, T, N) && T < BestT)
				{
					BestT = T;
					BestN = Xf.TransformVectorNoScale(N).GetSafeNormal();
					bHit = true;
				}
				if (RayTri(LO, LD, Bv, C, D, BestT, T, N) && T < BestT)
				{
					BestT = T;
					BestN = Xf.TransformVectorNoScale(N).GetSafeNormal();
					bHit = true;
				}
			}
		}
		if (bHit && BestT < Ts + StepCm)
		{
			break;
		}
	}
	if (!bHit)
	{
		return false;
	}
	OutHitCm = WorldOriginCm + Dir * BestT;
	OutNormal = BestN.IsNearlyZero() ? -Dir : BestN;
	return true;
}

void FGXCrustTiles::CollectLivePointsNear(const FVector& LocalM, float RadiusM, TArray<FVector>& Out) const
{
	if (RadiusM <= 0.0f || Live.Num() == 0)
	{
		return;
	}
	const float R2 = RadiusM * RadiusM;
	const float TileReach2 = FMath::Square(RadiusM + TileM + 4.0f);
	for (const auto& Pair : Live)
	{
		const FTile& Tile = Pair.Value;
		const FVector TileWorld = Tile.OriginCm * 0.01f;
		if (FVector::DistSquared(TileWorld, LocalM) > TileReach2)
		{
			continue;
		}
		TSet<int32> Used;
		for (const int32 Ix : Tile.Indices)
		{
			Used.Add(Ix);
		}
		for (const int32 I : Used)
		{
			if (!Tile.LivePos.IsValidIndex(I))
			{
				continue;
			}
			const FVector W = (Tile.OriginCm + Tile.LivePos[I]) * 0.01f;
			if (FVector::DistSquared(W, LocalM) <= R2)
			{
				Out.Add(W);
			}
		}
	}
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
	const float BaseCell = (Tile.FineCell > 0.1f) ? Tile.FineCell : CellM;
	const float Cell = BaseCell * static_cast<float>(1 << FMath::Max(0, Tile.Key.LOD));
	// Shared edge only. The extra overlap cell z-fought after sculpt
	// (texture/mesh thrash on 0.10.5). Nanite is off so the Y=0 crack is gone.
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
			// Stamp only. Density CSG is the cave volume — following it here
			// pulled leftover PlaceSphere pages into 8 m dirt pyramids (0.9.17 #1).
			(void)DensityAt;
			const float SurfR = Stamp.GetParams().Radius + Field.HeightM;
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
	Tile.QuadAlive.Init(true, Cells * Cells);

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
	// Nanite upgrade is 1/tick in Update — 25×320 ms on Ready froze spawn.
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
		DestroyTileVisuals(Live[K]);
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
	const bool bWasReady = bReady;
	bReady = Live.Contains(Center) && Live.Num() >= ReadyMin;
	if (bReady && !bWasReady)
	{
		ReadyAtSeconds = FPlatformTime::Seconds();
	}

	// One underfoot Nanite tile, and only after Ready has been up 2 s.
	// Cooking all 68 on load was 22 s at 3 FPS (0.10.0 / 0.10.1).
	int32 Upgraded = 0;
#if WITH_EDITOR
	const double Now = FPlatformTime::Seconds();
	if (CVarGXNaniteTiles.GetValueOnGameThread() > 0
		&& bReady
		&& ReadyAtSeconds > 0.0
		&& (Now - ReadyAtSeconds) > 2.0
		&& (Now - LastNaniteCookSeconds) > 2.0)
	{
		if (FTile* Under = Live.Find(Center))
		{
			if (!Under->bSculpted && !Under->NaniteComp.IsValid())
			{
				ApplyNaniteVisual(*Under, Material);
				LastNaniteCookSeconds = Now;
				Upgraded = 1;
			}
		}
	}
#endif

	if (Built > 0 || Upgraded > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles live=%d built=%d nanite+%d ready=%d face=%d u=%d v=%d"),
			Live.Num(), Built, Upgraded, bReady ? 1 : 0, Center.Face, Center.U, Center.V);
	}
}
