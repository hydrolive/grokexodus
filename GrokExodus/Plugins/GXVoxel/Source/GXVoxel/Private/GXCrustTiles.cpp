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
	1,
	TEXT("Walk tiles: 0=PMC visual, 1=Nanite tessellation + world-space displacement."),
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
		SMC->SetCastShadow(true);
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
	UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles: tile=%.0f cell=%.2f fine=%.2f stream=%.0f (0.10 Nanite displace)"),
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
	const TFunction<float(const FVector&)>& DensityAt)
{
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
	const float BrushSurf = LocalM.Size();
	const float Cover = RadiusM * 2.6f + 2.0f;
	const float Cover2 = Cover * Cover;
	int32 Changed = 0;
	for (auto& Pair : Live)
	{
		FTile& Tile = Pair.Value;
		const FVector TileWorld = Tile.OriginCm * 0.01f;
		if (FVector::DistSquared(TileWorld, LocalM) > FMath::Square(Cover + TileM + 4.0f))
		{
			continue;
		}
		// Rebuild from the stamp only. Following leftover CSG density (±8 m)
		// was the 0.9.17 pillar canyon — each 2 m vert climbed a saved sphere.
		if (FMath::Abs(Tile.FineCell - FineCellM) > 0.01f)
		{
			Tile.FineCell = FineCellM;
			BuildTile(Tile, Stamp, Material, nullptr);
		}
		UProceduralMeshComponent* Comp = Tile.Comp.Get();
		if (!Comp || Tile.LivePos.Num() == 0 || Tile.StampDir.Num() != Tile.LivePos.Num())
		{
			continue;
		}
		int32 N = 0;
		for (int32 I = 0; I < Tile.LivePos.Num(); ++I)
		{
			const FVector Dir = Tile.StampDir[I];
			const float Surf = Tile.StampSurfM.IsValidIndex(I) ? Tile.StampSurfM[I] : BrushSurf;
			const float D2 = FVector::DistSquared(Dir * Surf, BrushDir * BrushSurf);
			if (D2 > Cover2)
			{
				continue;
			}
			const float Dist = FMath::Sqrt(D2);
			float W = 1.0f - Dist / Cover;
			W = W * W * (3.0f - 2.0f * W);
			const float CurR = (Tile.OriginCm + Tile.LivePos[I]).Size() * 0.01f;
			const float Delta = RadiusM * 0.90f * W;
			float NewR = bRemove ? (CurR - Delta) : (CurR + Delta * 0.55f);
			NewR = bRemove ? FMath::Min(CurR, NewR) : FMath::Max(CurR, NewR);
			if (FMath::Abs(NewR - CurR) < 0.01f)
			{
				continue;
			}
			Tile.LivePos[I] = Dir * NewR * 100.0f - Tile.OriginCm;
			if (bRemove && W > 0.20f)
			{
				// Dirt (atlas 3), not forced rock (2). Rock on every sculpted
				// vert + radial N made cliffs one 35 m YZ grain (0.9.17 #3).
				if (Tile.UV0.IsValidIndex(I))
				{
					Tile.UV0[I] = FVector2D(3.0f, 0.0f);
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
		RecomputeNormals(Tile);
		Comp->CreateMeshSection_LinearColor(
			0, Tile.LivePos, Tile.Indices, Tile.LiveN, Tile.UV0, Tile.Colors, Tile.Tangents, true);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->UpdateBounds();
		ApplyNaniteVisual(Tile, Material);
		Changed += N;
	}
	if (Changed > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles sculpt %s verts=%d r=%.2f cell=%.2f"),
			bRemove ? TEXT("dig") : TEXT("place"), Changed, RadiusM, FineCellM);
		GX_PERF(1, TEXT("GX-tile sculpt %s verts=%d r=%.2f"),
			bRemove ? TEXT("dig") : TEXT("place"), Changed, RadiusM);
	}
	return Changed;
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

void FGXCrustTiles::DestroyTileVisuals(FTile& Tile)
{
	if (UStaticMeshComponent* SMC = Tile.NaniteComp.Get())
	{
		SMC->DestroyComponent();
	}
	Tile.NaniteComp.Reset();
	Tile.NaniteMesh.Reset();
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
	bReady = Live.Contains(Center) && Live.Num() >= ReadyMin;

	// PMC is walkable immediately. Nanite tessellation cooks one tile a tick
	// so the first frame is not a 8 s hitch (0.10.0 live: 25×320 ms).
	int32 Upgraded = 0;
	for (auto& Pair : Live)
	{
		if (Upgraded >= 1)
		{
			break;
		}
		if (Pair.Value.NaniteComp.IsValid())
		{
			continue;
		}
		ApplyNaniteVisual(Pair.Value, Material);
		++Upgraded;
	}

	if (Built > 0 || Upgraded > 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustTiles live=%d built=%d nanite+%d ready=%d face=%d u=%d v=%d"),
			Live.Num(), Built, Upgraded, bReady ? 1 : 0, Center.Face, Center.U, Center.V);
	}
}
