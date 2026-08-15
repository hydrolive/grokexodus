// Copyright Grok Exodus. All Rights Reserved.

#include "GXHorizonClipmap.h"
#include "GXVoxel.h"
#include "GXPerf.h"
#include "HAL/PlatformTime.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"

namespace
{
	UProceduralMeshComponent* MakeClipPMC(AActor* Owner)
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
		if (!PMC)
		{
			return nullptr;
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
		return PMC;
	}

	void AppendRimSkirts(
		TArray<FVector>& Positions,
		TArray<int32>& Indices,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UV0,
		TArray<FLinearColor>& Colors,
		TArray<FProcMeshTangent>& Tangents,
		const TArray<int32>& GridOf,
		int32 Dim,
		float CellM,
		float InnerM,
		float OuterM,
		float SinkM)
	{
		if (Dim < 2 || GridOf.Num() != Dim * Dim || Indices.Num() < 3)
		{
			return;
		}
		const int32 Half = Dim / 2;
		const float SkirtM = FMath::Max(SinkM, CellM * 0.4f) + 3.0f;
		const float SkirtCm = SkirtM * 100.0f;
		const float OuterRim0 = FMath::Square(FMath::Max(OuterM - CellM * 1.6f, 0.0f));
		const float InnerRim = (InnerM > 1.0f) ? FMath::Square(InnerM + CellM * 1.2f) : -1.0f;
		TSet<int32> RimSet;
		for (int32 J = 0; J < Dim; ++J)
		{
			for (int32 I = 0; I < Dim; ++I)
			{
				const int32 VI = GridOf[I + J * Dim];
				if (VI == INDEX_NONE)
				{
					continue;
				}
				const float U = (static_cast<float>(I - Half) + 0.5f) * CellM;
				const float V = (static_cast<float>(J - Half) + 0.5f) * CellM;
				const float R2 = U * U + V * V;
				if (R2 >= OuterRim0 || (InnerRim > 0.0f && R2 <= InnerRim))
				{
					RimSet.Add(VI);
				}
			}
		}
		if (RimSet.Num() == 0)
		{
			return;
		}
		TMap<int32, int32> DropOf;
		auto Dropped = [&](int32 VI) -> int32
		{
			if (const int32* Have = DropOf.Find(VI))
			{
				return *Have;
			}
			const FVector P = Positions[VI];
			FVector Rad = P.GetSafeNormal();
			if (Rad.IsNearlyZero())
			{
				Rad = FVector(1, 0, 0);
			}
			const FVector N = Normals.IsValidIndex(VI) ? Normals[VI] : Rad;
			const FVector2D UV = UV0.IsValidIndex(VI) ? UV0[VI] : FVector2D(2.0f, 0.0f);
			const FLinearColor Col = Colors.IsValidIndex(VI) ? Colors[VI] : FLinearColor(0.58f, 0.50f, 0.44f);
			const int32 Idx = Positions.Num();
			Positions.Add(P - Rad * SkirtCm);
			Normals.Add(N);
			UV0.Add(UV);
			Colors.Add(Col);
			FVector T = FVector::CrossProduct(Rad, FVector::ZAxisVector);
			if (T.SizeSquared() < 1e-6f)
			{
				T = FVector::CrossProduct(Rad, FVector::YAxisVector);
			}
			T.Normalize();
			Tangents.Add(FProcMeshTangent(T, false));
			DropOf.Add(VI, Idx);
			return Idx;
		};
		const int32 IndexCountBefore = Indices.Num();
		for (int32 T0 = 0; T0 + 2 < IndexCountBefore; T0 += 3)
		{
			const int32 IA = Indices[T0], IB = Indices[T0 + 1], IC = Indices[T0 + 2];
			const bool BA = RimSet.Contains(IA);
			const bool BB = RimSet.Contains(IB);
			const bool BC = RimSet.Contains(IC);
			if (BA && BB && !BC)
			{
				const int32 DA = Dropped(IA);
				const int32 DB = Dropped(IB);
				Indices.Add(IA); Indices.Add(IB); Indices.Add(DB);
				Indices.Add(IA); Indices.Add(DB); Indices.Add(DA);
			}
			else if (BB && BC && !BA)
			{
				const int32 DB = Dropped(IB);
				const int32 DC = Dropped(IC);
				Indices.Add(IB); Indices.Add(IC); Indices.Add(DC);
				Indices.Add(IB); Indices.Add(DC); Indices.Add(DB);
			}
			else if (BC && BA && !BB)
			{
				const int32 DC = Dropped(IC);
				const int32 DA = Dropped(IA);
				Indices.Add(IC); Indices.Add(IA); Indices.Add(DA);
				Indices.Add(IC); Indices.Add(DA); Indices.Add(DC);
			}
		}
	}

}

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
	// Fine inner ring so a 1.2 m brush moves several verts of THE crust.
	// A second edit mesh sat on the grass (0.7.35–37).
	// 0.8.3: 10/36/120 m read as stairs with hard dirt bands. Larger 2 m
	// disk, 8 m mid, cheaper far at 96 m. Shader slope blends materials.
	// Ring 1 must NOT be a full disk. InnerM=0 sat the 8 m grass 2 m
	// under every walk quad — punch a 2 m hole and you get an undiggable
	// "core" floor plus leftover floating tris (0.8.6 shot). Hole stays
	// around the viewer; rebuild ring 1 with ring 0 so the hole moves.
	const FSpec Specs[] = {
		{ 0.0f, 180.0f, 2.0f, 0.0f },
		{ 160.0f, 700.0f, 8.0f, 2.0f },
		{ 660.0f, 2800.0f, 32.0f, 3.2f },
		{ 2600.0f, 10000.0f, 96.0f, 6.0f },
	};
	for (const FSpec& S : Specs)
	{
		UProceduralMeshComponent* PMC = MakeClipPMC(Owner);
		if (!PMC)
		{
			continue;
		}
		FRing Ring;
		Ring.Comp = PMC;
		Ring.InnerM = S.Inner;
		Ring.OuterM = S.Outer;
		Ring.CellM = S.Cell;
		Ring.SinkM = S.Sink;
		Rings.Add(Ring);
	}
	// Ring 0 is the walk surface we punch. Async cook delayed the hole
	// and left the old lid up (the 0.7.43 shell).
	if (Rings.Num() > 0)
	{
		if (UProceduralMeshComponent* C = Rings[0].Comp.Get())
		{
			C->bUseAsyncCooking = false;
		}
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap: %d rings (0.8 HLOD + rim skirts)"), Rings.Num());
}

void FGXHorizonClipmap::Invalidate()
{
	LastViewerLocal = FVector(1e12f, 0, 0);
	bEditsDirty = true;
	for (FRing& R : Rings)
	{
		R.LastBuild = FVector(1e12f, 0, 0);
	}
}

void FGXHorizonClipmap::NotifyEdits()
{
	bEditsDirty = true;
}

void FGXHorizonClipmap::NotifyBrush(
	const FVector& LocalM,
	float RadiusM,
	TFunction<float(const FVector&)> DensityAt)
{
	if (Rings.Num() == 0 || RadiusM <= 0.0f)
	{
		return;
	}
	(void)DensityAt;
	FRing& Ring = Rings[0];
	UProceduralMeshComponent* Comp = Ring.Comp.Get();
	if (!Comp || Ring.StampDir.Num() == 0)
	{
		return;
	}
	if (Ring.LivePos.Num() < Ring.StampDir.Num()
		|| Ring.LiveN.Num() != Ring.LivePos.Num()
		|| Ring.UV0.Num() != Ring.LivePos.Num())
	{
		return;
	}
	const float R2 = RadiusM * RadiusM;
	const float Cover = RadiusM + FMath::Max(Ring.CellM, 2.0f);
	const float Cover2 = Cover * Cover;
	FVector BrushDir = LocalM.GetSafeNormal();
	if (BrushDir.IsNearlyZero())
	{
		BrushDir = FVector(1, 0, 0);
	}

	int32 Dropped = 0;
	for (int32 VI = 0; VI < Ring.StampDir.Num(); ++VI)
	{
		if (!Ring.StampSurfM.IsValidIndex(VI) || !Ring.LivePos.IsValidIndex(VI))
		{
			continue;
		}
		const FVector Dir = Ring.StampDir[VI];
		const float Surf = Ring.StampSurfM[VI];
		const FVector AtSurf = Dir * Surf;
		const FVector BrushOnSurf = BrushDir * Surf;
		if (FVector::DistSquared(AtSurf, BrushOnSurf) > Cover2)
		{
			continue;
		}
		// Geometric CSG on this radial: drop to the inner sphere hit.
		// Density sampling at 2 m verts missed a 1.2 m ball and only
		// stained the texture (0.8.15 #2).
		const float Along = FVector::DotProduct(LocalM, Dir);
		const float Perp2 = FMath::Max(0.0f, LocalM.SizeSquared() - Along * Along);
		if (Perp2 >= R2)
		{
			continue;
		}
		const float Half = FMath::Sqrt(R2 - Perp2);
		const float FloorR = Along - Half - 0.08f;
		if (FloorR >= Surf - 0.05f)
		{
			continue;
		}
		const float R = FMath::Max(FloorR, Surf - 48.0f);
		Ring.LivePos[VI] = Dir * R * 100.0f;
		if (Ring.UV0.IsValidIndex(VI))
		{
			Ring.UV0[VI] = FVector2D(2.0f, 0.0f); // rock
		}
		if (Ring.Colors.IsValidIndex(VI))
		{
			Ring.Colors[VI] = FLinearColor(0.58f, 0.50f, 0.44f, 1.0f);
		}
		++Dropped;
	}
	if (Dropped == 0 || Ring.LivePos.Num() < 3)
	{
		return;
	}
	Comp->UpdateMeshSection_LinearColor(
		0, Ring.LivePos, Ring.LiveN, Ring.UV0, Ring.Colors, Ring.Tangents);
	GX_PERF(1, TEXT("GX-clipmap brush drop verts=%d r=%.2f"), Dropped, RadiusM);
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
	bEditsDirty = false;
}

void FGXHorizonClipmap::BuildRing(
	FRing& Ring,
	const FGXSphereStamp& Stamp,
	const FVector& CenterDir,
	const FVector& Tangent,
	const FVector& Bitangent,
	UMaterialInterface* Material,
	const FGXCrustAtlas* Atlas,
	const TFunction<float(const FVector&)>& DensityAt)
{
	UProceduralMeshComponent* Comp = Ring.Comp.Get();
	if (!Comp)
	{
		return;
	}

	const float InnerM = Ring.InnerM;
	const float OuterM = Ring.OuterM;
	const float CellM = Ring.CellM;
	const float SinkM = Ring.SinkM;
	const int32 Half = FMath::Max(8, FMath::CeilToInt(OuterM / CellM));
	const int32 Dim = Half * 2 + 1;
	const float R0 = Stamp.GetParams().Radius;
	const float Relief = FMath::Max(Stamp.GetParams().MaxRelief, 1.0f);
	const float InnerPad = InnerM * InnerM;
	const float OuterPad = OuterM * OuterM;
	// Ring 0 must sit on the stamp. Max(., 0.5) put the walk surface 50 cm
	// under the brush patch — add/remove floated above the grass.
	// Only the 2 m walk ring sits on the stamp. Ring 1 is a full disk
	// (InnerM=0) so look-back has no hole — if we also force sink 0 it
	// becomes an uncut lid over every dig (0.7.54).
	const float Sink = (CellM <= 3.0f) ? 0.0f : FMath::Max(SinkM, 0.5f);

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

	Ring.StampPos.Reset();
	Ring.LivePos.Reset();
	Ring.StampDir.Reset();
	Ring.StampSurfM.Reset();
	Ring.UV0.Reset();
	Ring.Colors.Reset();
	Ring.Tangents.Reset();
	Ring.LiveN.Reset();
	Ring.StampIndices.Reset();
	Ring.GridOf.Reset();
	Ring.GridDim = 0;
	Ring.SinkUsed = Sink;

	TArray<int32> IndexOf;
	IndexOf.Init(INDEX_NONE, Dim * Dim);

	for (int32 J = 0; J < Dim; ++J)
	{
		// Half-cell offset so a grid edge does not run through the pawn
		// (that was the dark seam down the view in 0.7.31).
		const float V = (static_cast<float>(J - Half) + 0.5f) * CellM;
		for (int32 I = 0; I < Dim; ++I)
		{
			const float U = (static_cast<float>(I - Half) + 0.5f) * CellM;
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
			// One height function for every ring. Mixing atlas + stamp made a crease.
			const float HeightM = Field.HeightM;
			(void)Atlas;
			(void)DensityAt;
			float SurfR = Stamp.GetParams().Radius + HeightM;
			const FVector P = Dir * (SurfR - Sink) * 100.0f;
			const int32 Idx = I + J * Dim;
			IndexOf[Idx] = Positions.Num();
			Positions.Add(P);
			Normals.Add(Dir);
			Ring.StampPos.Add(P);
			Ring.StampDir.Add(Dir);
			Ring.StampSurfM.Add(SurfR);
			// Always grass atlas cell. Hard 1/2/3 ids made 8–36 m quads
			// into blocky dirt/rock slabs (#1/#2). PBR slope blends dirt/rock.
			const float AtlasId = 1.0f;
			UV0.Add(FVector2D(AtlasId, 0.0f));
			Colors.Add(FLinearColor(0.52f, 0.60f, 0.34f, 1.0f));
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

	const int32 StampTriEnd = Indices.Num();

	// Face normals + slope colors. Do not use radial N — that made far PBR sample
	// the 2 m grass/volcanic atlas on 72 m triangles (red tiled sheet).
	TArray<FVector> AccN;
	AccN.Init(FVector::ZeroVector, Positions.Num());
	for (int32 T0 = 0; T0 + 2 < StampTriEnd; T0 += 3)
	{
		const int32 IA = Indices[T0], IB = Indices[T0 + 1], IC = Indices[T0 + 2];
		const FVector FN = FVector::CrossProduct(Positions[IB] - Positions[IA], Positions[IC] - Positions[IA]);
		AccN[IA] += FN; AccN[IB] += FN; AccN[IC] += FN;
	}
	// Enough radial to keep rolling hills grassy; not so much that mountains
	// lose slope and the shader cannot blend to rock.
	const float KeepRadial = (CellM > 3.0f) ? FMath::Clamp((CellM - 3.0f) / 80.0f, 0.08f, 0.35f) : 0.0f;
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
		if (KeepRadial > 0.0f && !Radial.IsNearlyZero())
		{
			N = (N * (1.0f - KeepRadial) + Radial * KeepRadial).GetSafeNormal();
		}
		Normals[V] = N;
		const float Slope = 1.0f - FMath::Abs(FVector::DotProduct(N, Radial));
		const float HeightM = Positions[V].Size() * 0.01f + Sink - R0;
		const float Alt = HeightM / Relief;
		const float Biome = UV0[V].X;
		// No snow-white: yellow atmosphere turned it into the teal gumdrop.
		if (Biome > 1.5f || Alt > 0.16f || Slope > 0.14f)
		{
			Colors[V] = FLinearColor(0.58f, 0.50f, 0.44f); // rock
		}
		else if (Slope > 0.09f)
		{
			Colors[V] = FLinearColor(0.54f, 0.42f, 0.28f); // dirt skirt
		}
		else
		{
			Colors[V] = FLinearColor(0.58f, 0.66f, 0.38f); // grass — valleys were too dark
		}
	}

	Ring.StampIndices = Indices;

	AppendRimSkirts(Positions, Indices, Normals, UV0, Colors, Tangents,
		IndexOf, Dim, CellM, InnerM, OuterM, SinkM);
	// Store AFTER skirts. UpdateMeshSection must keep this count —
	// 0.8.12 wrote grid-only arrays and the grass lid never moved.
	Ring.LivePos = Positions;
	Ring.LiveN = Normals;
	Ring.UV0 = UV0;
	Ring.Colors = Colors;
	Ring.Tangents = Tangents;
	Ring.GridOf = IndexOf;
	Ring.GridDim = Dim;
	Ring.SinkUsed = Sink;

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

void FGXHorizonClipmap::ApplyRingEdits(
	FRing& Ring,
	const FGXSphereStamp& Stamp,
	UMaterialInterface* Material,
	const TFunction<bool(const FVector&)>& ShouldCut,
	const TFunction<float(const FVector&)>& DensityAt,
	const TFunction<bool(const FVector&)>& HasCaveMesh)
{
	UProceduralMeshComponent* Comp = Ring.Comp.Get();
	if (!Comp || Ring.StampPos.Num() == 0 || Ring.StampIndices.Num() < 3)
	{
		return;
	}
	(void)Stamp;
	(void)Material;
	(void)HasCaveMesh;

	// Never delete quads. 0.8.9–0.8.11 punch opened 400+ faces on a hill
	// (saved air still in the 180 m disk, no mesh to fill). Drop only.
	if (!ShouldCut || !DensityAt || Ring.CellM > 3.0f)
	{
		return;
	}
	if (Ring.LivePos.Num() < Ring.StampDir.Num()
		|| Ring.LiveN.Num() != Ring.LivePos.Num()
		|| Ring.UV0.Num() != Ring.LivePos.Num())
	{
		return;
	}

	int32 Dropped = 0;
	for (int32 VI = 0; VI < Ring.StampDir.Num(); ++VI)
	{
		if (!Ring.StampSurfM.IsValidIndex(VI))
		{
			continue;
		}
		const FVector Dir = Ring.StampDir[VI];
		const float Surf = Ring.StampSurfM[VI];
		bool bCut = ShouldCut(Dir * Surf);
		if (!bCut)
		{
			for (float D = 0.5f; D <= 2.5f && !bCut; D += 0.5f)
			{
				bCut = ShouldCut(Dir * (Surf - D));
			}
		}
		if (!bCut)
		{
			continue;
		}
		float R = Surf;
		for (int32 Step = 0; Step < 160; ++Step)
		{
			if (DensityAt(Dir * R) > 0.05f)
			{
				break;
			}
			R -= 0.25f;
			if (R < Surf - 48.0f)
			{
				break;
			}
		}
		if (FMath::Abs(R - Surf) < 0.05f)
		{
			continue;
		}
		R -= 0.08f;
		Ring.LivePos[VI] = Dir * R * 100.0f;
		if (Ring.UV0.IsValidIndex(VI))
		{
			Ring.UV0[VI] = FVector2D(2.0f, 0.0f);
		}
		if (Ring.Colors.IsValidIndex(VI))
		{
			Ring.Colors[VI] = FLinearColor(0.58f, 0.50f, 0.44f, 1.0f);
		}
		++Dropped;
	}
	if (Dropped == 0)
	{
		return;
	}
	Comp->UpdateMeshSection_LinearColor(
		0, Ring.LivePos, Ring.LiveN, Ring.UV0, Ring.Colors, Ring.Tangents);
	GX_PERF(1, TEXT("GX-clipmap rebuild-drop verts=%d"), Dropped);
}

#if 0
void FGXHorizonClipmap::ApplyRingEdits_REMOVED_PUNCH(
	auto CutAt = [&](const FVector& Dir, float Surf) -> bool
	{
		if (Dir.IsNearlyZero())
		{
			return false;
		}
		return ShouldCut(Dir * Surf) || ShouldCut(Dir * (Surf - 0.8f));
	};

	// 0.8.8 draped the grass heightfield into the hole (overhang, grass
	// crater, z-fight shards) and the mound search *raised* rim verts.
	// Open air quads so the voxel cave (dirt/rock) is the surface.
	// Only drop when the cave mesh is not ready yet. Never search up.
	TArray<int32> Indices;
	Indices.Reserve(Ring.StampIndices.Num());
	TArray<uint8> Mark;
	Mark.SetNumZeroed(Positions.Num());
	auto MarkIf = [&](int32 VI)
	{
		if (Mark.IsValidIndex(VI))
		{
			Mark[VI] = 1;
		}
	};

	auto EdgeCut = [&](int32 VA, int32 VB) -> bool
	{
		if (!Ring.StampDir.IsValidIndex(VA) || !Ring.StampDir.IsValidIndex(VB)
			|| !Ring.StampSurfM.IsValidIndex(VA) || !Ring.StampSurfM.IsValidIndex(VB))
		{
			return false;
		}
		FVector Dir = (Ring.StampDir[VA] + Ring.StampDir[VB]).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = Ring.StampDir[VA];
		}
		return CutAt(Dir, 0.5f * (Ring.StampSurfM[VA] + Ring.StampSurfM[VB]));
	};
	auto MeshAt = [&](const FVector& P) -> bool
	{
		return HasCaveMesh && HasCaveMesh(P);
	};

	const int32 QuadN = Ring.StampIndices.Num() / 6;
	TArray<uint8> QuadOpen;
	QuadOpen.SetNumZeroed(QuadN);

	int32 Qi = 0;
	for (int32 T0 = 0; T0 + 5 < Ring.StampIndices.Num(); T0 += 6, ++Qi)
	{
		const int32 A = Ring.StampIndices[T0];
		const int32 B = Ring.StampIndices[T0 + 1];
		const int32 C = Ring.StampIndices[T0 + 2];
		const int32 D = Ring.StampIndices[T0 + 4];
		if (!Ring.StampDir.IsValidIndex(A) || !Ring.StampDir.IsValidIndex(B)
			|| !Ring.StampDir.IsValidIndex(C) || !Ring.StampDir.IsValidIndex(D)
			|| !Ring.StampSurfM.IsValidIndex(A) || !Ring.StampSurfM.IsValidIndex(B)
			|| !Ring.StampSurfM.IsValidIndex(C) || !Ring.StampSurfM.IsValidIndex(D))
		{
			continue;
		}
		FVector MidDir = (
			Ring.StampDir[A] + Ring.StampDir[B]
			+ Ring.StampDir[C] + Ring.StampDir[D]).GetSafeNormal();
		if (MidDir.IsNearlyZero())
		{
			MidDir = Ring.StampDir[A];
		}
		const float MidSurf = 0.25f * (
			Ring.StampSurfM[A] + Ring.StampSurfM[B]
			+ Ring.StampSurfM[C] + Ring.StampSurfM[D]);
		// 9 samples: a 1.2 m brush often sits between verts and the mid
		// (0.8.9 leftover grass plane on the new dirt).
		const bool bAir = CutAt(MidDir, MidSurf)
			|| CutAt(Ring.StampDir[A], Ring.StampSurfM[A])
			|| CutAt(Ring.StampDir[B], Ring.StampSurfM[B])
			|| CutAt(Ring.StampDir[C], Ring.StampSurfM[C])
			|| CutAt(Ring.StampDir[D], Ring.StampSurfM[D])
			|| EdgeCut(A, B) || EdgeCut(B, D) || EdgeCut(D, C) || EdgeCut(C, A);
		const FVector MidP = MidDir * MidSurf;
		// Must have a live cave mesh. Opening every saved-air quad in the
		// 180 m disk (0.8.10) left holes on hills — those chunks were evicted.
		const bool bMesh = MeshAt(MidP)
			|| MeshAt(Ring.StampDir[A] * Ring.StampSurfM[A])
			|| MeshAt(Ring.StampDir[B] * Ring.StampSurfM[B])
			|| MeshAt(Ring.StampDir[C] * Ring.StampSurfM[C])
			|| MeshAt(Ring.StampDir[D] * Ring.StampSurfM[D]);
		if (bAir && bMesh)
		{
			QuadOpen[Qi] = 1;
		}
	}

	// One edge of dilate: a solid-mid quad that shares an edge with the
	// hole is the thin grass plane on the new voxels.
	TArray<uint8> VertOnOpen;
	VertOnOpen.SetNumZeroed(Positions.Num());
	Qi = 0;
	for (int32 T0 = 0; T0 + 5 < Ring.StampIndices.Num(); T0 += 6, ++Qi)
	{
		if (!QuadOpen.IsValidIndex(Qi) || !QuadOpen[Qi])
		{
			continue;
		}
		MarkIf(Ring.StampIndices[T0]);
		MarkIf(Ring.StampIndices[T0 + 1]);
		MarkIf(Ring.StampIndices[T0 + 2]);
		MarkIf(Ring.StampIndices[T0 + 4]);
		if (VertOnOpen.IsValidIndex(Ring.StampIndices[T0])) VertOnOpen[Ring.StampIndices[T0]] = 1;
		if (VertOnOpen.IsValidIndex(Ring.StampIndices[T0 + 1])) VertOnOpen[Ring.StampIndices[T0 + 1]] = 1;
		if (VertOnOpen.IsValidIndex(Ring.StampIndices[T0 + 2])) VertOnOpen[Ring.StampIndices[T0 + 2]] = 1;
		if (VertOnOpen.IsValidIndex(Ring.StampIndices[T0 + 4])) VertOnOpen[Ring.StampIndices[T0 + 4]] = 1;
	}
	Qi = 0;
	for (int32 T0 = 0; T0 + 5 < Ring.StampIndices.Num(); T0 += 6, ++Qi)
	{
		if (QuadOpen.IsValidIndex(Qi) && QuadOpen[Qi])
		{
			continue;
		}
		const int32 Corners[4] = {
			Ring.StampIndices[T0],
			Ring.StampIndices[T0 + 1],
			Ring.StampIndices[T0 + 2],
			Ring.StampIndices[T0 + 4]
		};
		int32 Hit = 0;
		for (int32 K = 0; K < 4; ++K)
		{
			if (VertOnOpen.IsValidIndex(Corners[K]) && VertOnOpen[Corners[K]])
			{
				++Hit;
			}
		}
		if (Hit >= 2 && QuadOpen.IsValidIndex(Qi))
		{
			QuadOpen[Qi] = 1;
			for (int32 K = 0; K < 4; ++K)
			{
				MarkIf(Corners[K]);
			}
		}
	}

	int32 Punched = 0;
	Qi = 0;
	for (int32 T0 = 0; T0 + 5 < Ring.StampIndices.Num(); T0 += 6, ++Qi)
	{
		if (QuadOpen.IsValidIndex(Qi) && QuadOpen[Qi])
		{
			++Punched;
			continue;
		}
		for (int32 K = 0; K < 6; ++K)
		{
			Indices.Add(Ring.StampIndices[T0 + K]);
		}
	}

	int32 Dropped = 0;
	if (DensityAt)
	{
		for (int32 VI = 0; VI < Positions.Num(); ++VI)
		{
			if (!Mark.IsValidIndex(VI) || !Mark[VI])
			{
				continue;
			}
			if (!Ring.StampDir.IsValidIndex(VI) || !Ring.StampSurfM.IsValidIndex(VI))
			{
				continue;
			}
			const FVector Dir = Ring.StampDir[VI];
			const float Surf = Ring.StampSurfM[VI];
			const bool bAirBelow = DensityAt(Dir * (Surf - 0.4f)) <= 0.05f
				|| DensityAt(Dir * (Surf - 0.8f)) <= 0.05f
				|| CutAt(Dir, Surf);
			// Solid rim with no air under it stays. The 0.8.8 up-search
			// heaved those verts into floating spikes.
			if (DensityAt(Dir * Surf) > 0.05f && !bAirBelow)
			{
				continue;
			}
			float R = Surf;
			for (int32 Step = 0; Step < 160; ++Step)
			{
				if (DensityAt(Dir * R) > 0.05f)
				{
					break;
				}
				R -= 0.25f;
				if (R < Surf - 48.0f)
				{
					break;
				}
			}
			if (FMath::Abs(R - Surf) < 0.05f)
			{
				continue;
			}
			Positions[VI] = Dir * R * 100.0f;
			++Dropped;
		}
	}

	if (Punched == 0 && Dropped == 0)
	{
		return;
	}

	TArray<FVector> AccN;
	AccN.Init(FVector::ZeroVector, Positions.Num());
	for (int32 T0 = 0; T0 + 2 < Indices.Num(); T0 += 3)
	{
		const int32 IA = Indices[T0], IB = Indices[T0 + 1], IC = Indices[T0 + 2];
		if (!Positions.IsValidIndex(IA) || !Positions.IsValidIndex(IB) || !Positions.IsValidIndex(IC))
		{
			continue;
		}
		const FVector FN = FVector::CrossProduct(Positions[IB] - Positions[IA], Positions[IC] - Positions[IA]);
		AccN[IA] += FN;
		AccN[IB] += FN;
		AccN[IC] += FN;
	}
	for (int32 V = 0; V < Positions.Num(); ++V)
	{
		FVector N = AccN[V].GetSafeNormal();
		if (N.IsNearlyZero())
		{
			N = Normals.IsValidIndex(V) ? Normals[V] : Positions[V].GetSafeNormal();
		}
		const FVector Radial = Positions[V].GetSafeNormal();
		if (!Radial.IsNearlyZero() && FVector::DotProduct(N, Radial) < 0.0f)
		{
			N = -N;
		}
		if (Normals.IsValidIndex(V))
		{
			Normals[V] = N;
		}
	}

	if (Ring.GridDim >= 2 && Ring.GridOf.Num() == Ring.GridDim * Ring.GridDim)
	{
		AppendRimSkirts(Positions, Indices, Normals, UV0, Colors, Tangents,
			Ring.GridOf, Ring.GridDim, Ring.CellM, Ring.InnerM, Ring.OuterM, Ring.SinkM);
	}

	if (Positions.Num() < 3 || Indices.Num() < 3)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap edit left empty ring"));
		return;
	}

	Comp->CreateMeshSection_LinearColor(0, Positions, Indices, Normals, UV0, Colors, Tangents, false);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->SetVisibility(true);
	Comp->SetHiddenInGame(false);
	Comp->UpdateBounds();
	GX_PERF(1, TEXT("GX-clipmap edit cell=%.0f punch=%d drop=%d tris=%d"),
		Ring.CellM, Punched, Dropped, Indices.Num() / 3);
}
#endif

void FGXHorizonClipmap::Update(
	AActor* Owner,
	const FGXSphereStamp& Stamp,
	const FVector& ViewerLocalM,
	float InnerHoleM,
	float OuterM,
	UMaterialInterface* NearMaterial,
	UMaterialInterface* FarMaterial,
	UMaterialInterface* PatchMaterial,
	const FGXCrustAtlas* Atlas,
	TFunction<bool(const FVector&)> ShouldPunch,
	TFunction<float(const FVector&)> DensityAt,
	TFunction<bool(const FVector&)> HasCaveMesh)
{
	if (!Owner || Rings.Num() == 0)
	{
		return;
	}
	(void)PatchMaterial;

	UMaterialInterface* NearLit = NearMaterial;
	UMaterialInterface* FarLit = FarMaterial;
	if (!FarLit)
	{
		FarLit = NearLit;
	}
	if (!FarLit)
	{
		FarLit = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Voxel/Materials/M_VoxelTerrain_PBR.M_VoxelTerrain_PBR"));
	}
	if (!FarLit)
	{
		FarLit = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Voxel/Materials/M_VoxelHorizonFar.M_VoxelHorizonFar"));
	}
	if (!NearLit)
	{
		NearLit = FarLit;
	}

	if (bEditsDirty)
	{
		for (FRing& Ring : Rings)
		{
			ApplyRingEdits(Ring, Stamp, (Ring.CellM <= 3.0f) ? NearLit : FarLit, ShouldPunch, DensityAt, HasCaveMesh);
		}
		bEditsDirty = false;
	}

	// Do not remesh the walk ring on every brush tick — that was the wobble.
	if (FVector::DistSquared(ViewerLocalM, LastViewerLocal) < FMath::Square(60.0f) && bReady)
	{
		return;
	}

	FVector CenterDir = ViewerLocalM.GetSafeNormal();
	if (CenterDir.IsNearlyZero())
	{
		CenterDir = FVector(1, 0, 0);
	}
	FVector T, B;
	CenterDir.FindBestAxisVectors(T, B);

	// Full disk. A hole was the teal river / island cliff.
	if (Rings.Num() > 0)
	{
		Rings[0].InnerM = FMath::Max(0.0f, InnerHoleM);
	}
	if (Rings.Num() > 2)
	{
		Rings.Last().OuterM = FMath::Max(OuterM, 4000.0f);
	}

	const double T0 = FPlatformTime::Seconds();
	int32 Built = 0;
	for (int32 I = 0; I < Rings.Num(); ++I)
	{
		FRing& Ring = Rings[I];
		const float RebuildM = (I == 0) ? 70.0f : (I == 1) ? 70.0f : 400.0f;
		if (bReady && Built >= 1 && I > 1)
		{
			break;
		}
		if (bReady && FVector::DistSquared(ViewerLocalM, Ring.LastBuild) < FMath::Square(RebuildM))
		{
			continue;
		}
		if (UProceduralMeshComponent* C = Ring.Comp.Get())
		{
			UMaterialInterface* UseMat = (I == 0) ? NearLit : FarLit;
			BuildRing(Ring, Stamp, CenterDir, T, B, UseMat, Atlas, DensityAt);
			if (ShouldPunch)
			{
				ApplyRingEdits(Ring, Stamp, UseMat, ShouldPunch, DensityAt, HasCaveMesh);
			}
			Ring.LastBuild = ViewerLocalM;
			++Built;
		}
	}
	bEditsDirty = false;
	LastViewerLocal = ViewerLocalM;
	bReady = true;
	if (Built == 0)
	{
		return;
	}
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap rebuilt inner=%.0f outer=%.0f ms=%.1f rings=%d"),
		Rings[0].InnerM, Rings.Last().OuterM, Ms, Built);
	GX_PERF(1, TEXT("GX-clipmap rebuild ms=%.1f inner=%.0f outer=%.0f built=%d"),
		Ms, Rings[0].InnerM, Rings.Last().OuterM, Built);
}
