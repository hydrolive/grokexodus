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

	/** Ring-0 cell. Hide + patch share this so a 2 m fade cannot open a halo. */
	constexpr float GClipCellM = 2.0f;

	/** High-res disk that replaces clipmap under a brush (metres). */
	float EditCoverM(float Rad)
	{
		// Two clip cells past R: every 2 m quad that touches the sphere is
		// inside the patch, and the vertex-A fade (one cell) stays on the
		// patch skirt — not as a hole or a coarse dent.
		return Rad + 2.0f * GClipCellM + 0.5f;
	}

	/** W<0 bowl, W>0 cap. Offset the *local* stamp, not HoleR. */
	float ApplyEditSpheres(float SurfR, const FVector& Guess, const TArray<FVector4>* Edits)
	{
		if (!Edits || Edits->Num() == 0)
		{
			return SurfR;
		}
		for (const FVector4& E : *Edits)
		{
			const float Rad = FMath::Abs(E.W);
			if (Rad < 0.05f)
			{
				continue;
			}
			const FVector C(E.X, E.Y, E.Z);
			const float D2 = FVector::DistSquared(Guess, C);
			if (D2 >= Rad * Rad)
			{
				continue;
			}
			const float Drop = FMath::Sqrt(FMath::Max(0.0f, Rad * Rad - D2));
			// HoleR ± drop put the rim at the hit radius on a slope
			// (add trim that missed the grass). Local stamp ± drop
			// keeps the rim on the landscape.
			if (E.W < 0.0f)
			{
				SurfR -= Drop;
			}
			else
			{
				SurfR += Drop;
			}
		}
		return SurfR;
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
	const FSpec Specs[] = {
		{ 0.0f, 140.0f, 2.0f, 0.0f },
		{ 120.0f, 560.0f, 8.0f, 1.0f },
		{ 520.0f, 2400.0f, 24.0f, 2.5f },
		{ 2200.0f, 10000.0f, 72.0f, 4.5f },
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
	EditPatch = MakeClipPMC(Owner);
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap: %d rings + edit patch"), Rings.Num());
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
	if (UProceduralMeshComponent* C = EditPatch.Get())
	{
		C->DestroyComponent();
	}
	EditPatch.Reset();
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
	const float Sink = (InnerM < 1.0f) ? 0.0f : FMath::Max(SinkM, 0.5f);

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
	Ring.StampDir.Reset();
	Ring.StampSurfM.Reset();
	Ring.UV0.Reset();
	Ring.Colors.Reset();
	Ring.Tangents.Reset();
	Ring.LiveN.Reset();
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
			// Same atlas ids the near PBR reads from UV0.X (1 grass, 3 dirt, 2 rock).
			float AtlasId = 1.0f;
			if (Field.SlopeProxy > 0.16f || Field.Orogeny > 0.15f || Field.Volcano > 0.18f)
			{
				AtlasId = 2.0f;
			}
			else if (Field.SlopeProxy > 0.18f)
			{
				AtlasId = 3.0f;
			}
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

	Ring.UV0 = UV0;
	Ring.Colors = Colors;
	Ring.Tangents = Tangents;
	Ring.LiveN = Normals;
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

void FGXHorizonClipmap::ApplyRingEdits(FRing& Ring, const TArray<FVector4>* Edits)
{
	UProceduralMeshComponent* Comp = Ring.Comp.Get();
	if (!Comp || Ring.StampPos.Num() == 0)
	{
		return;
	}
	// Keep stamp height (no coarse dent). Hide the lid by vertex alpha so
	// the high-res bowl is visible. Shader WP masks never punched.
	// A=0 only inside (Cover − cell) so the interpolated clip (~0.333)
	// lands inside the patch, not as a halo past it.
	for (int32 I = 0; I < Ring.StampPos.Num(); ++I)
	{
		float A = 1.0f;
		if (Edits)
		{
			const FVector Guess = Ring.StampDir[I] * Ring.StampSurfM[I];
			for (const FVector4& E : *Edits)
			{
				const float Rad = FMath::Abs(E.W);
				if (Rad < 0.05f)
				{
					continue;
				}
				const float Hide = FMath::Max(Rad, EditCoverM(Rad) - Ring.CellM);
				if (FVector::DistSquared(Guess, FVector(E.X, E.Y, E.Z)) < Hide * Hide)
				{
					A = 0.0f;
					break;
				}
			}
		}
		Ring.Colors[I].A = A;
	}
	Comp->UpdateMeshSection_LinearColor(0, Ring.StampPos, Ring.LiveN, Ring.UV0, Ring.Colors, Ring.Tangents);
}

void FGXHorizonClipmap::BuildEditPatch(
	UProceduralMeshComponent* Comp,
	const FGXSphereStamp& Stamp,
	UMaterialInterface* Material,
	const TArray<FVector4>* EditHolesLocalM)
{
	if (!Comp)
	{
		return;
	}
	Comp->ClearAllMeshSections();
	if (!EditHolesLocalM || EditHolesLocalM->Num() == 0)
	{
		Comp->SetVisibility(false);
		return;
	}

	const float R0 = Stamp.GetParams().Radius;
	const float CellM = 0.22f;
	constexpr int32 MaxPatches = 8;
	const int32 Start = FMath::Max(0, EditHolesLocalM->Num() - MaxPatches);
	int32 Sections = 0;
	int32 TotalVerts = 0;

	for (int32 P = Start; P < EditHolesLocalM->Num(); ++P)
	{
		const FVector4& E = (*EditHolesLocalM)[P];
		const float Rad = FMath::Abs(E.W);
		if (Rad < 0.05f)
		{
			continue;
		}
		const FVector Hole(E.X, E.Y, E.Z);
		FVector CenterDir = Hole.GetSafeNormal();
		if (CenterDir.IsNearlyZero())
		{
			CenterDir = FVector(1, 0, 0);
		}
		FVector TanU, TanV;
		CenterDir.FindBestAxisVectors(TanU, TanV);
		// Same disk ApplyRingEdits hides, plus one 2 m cell of A fade.
		// Outside R this is stamp height — rim sits on the grass.
		const float Cover = EditCoverM(Rad);
		const int32 Half = FMath::Max(4, FMath::CeilToInt(Cover / CellM));
		const int32 Dim = Half * 2 + 1;
		const float Cover2 = Cover * Cover;

		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colors;
		TArray<int32> Indices;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> IndexOf;
		IndexOf.Init(INDEX_NONE, Dim * Dim);

		for (int32 J = 0; J < Dim; ++J)
		{
			const float V = (static_cast<float>(J - Half) + 0.5f) * CellM;
			for (int32 I = 0; I < Dim; ++I)
			{
				const float U = (static_cast<float>(I - Half) + 0.5f) * CellM;
				if (U * U + V * V > Cover2)
				{
					continue;
				}
				FVector Dir = (CenterDir * R0 + TanU * U + TanV * V).GetSafeNormal();
				if (Dir.IsNearlyZero())
				{
					Dir = CenterDir;
				}
				const FGXEarthField Field = Stamp.SampleEarthField(FVector3f(Dir.X, Dir.Y, Dir.Z), false);
				float SurfR = R0 + Field.HeightM;
				SurfR = ApplyEditSpheres(SurfR, Dir * SurfR, EditHolesLocalM);
				IndexOf[I + J * Dim] = Positions.Num();
				Positions.Add(Dir * SurfR * 100.0f);
				Normals.Add(Dir);
				float AtlasId = 1.0f;
				if (Field.SlopeProxy > 0.16f || Field.Orogeny > 0.15f || Field.Volcano > 0.18f)
				{
					AtlasId = 2.0f;
				}
				UV0.Add(FVector2D(AtlasId, 0.0f));
				Colors.Add(FLinearColor(0.52f, 0.60f, 0.34f, 1.0f));
				FVector Tan = FVector::CrossProduct(Dir, FVector::ZAxisVector);
				if (Tan.SizeSquared() < 1e-6f)
				{
					Tan = FVector::CrossProduct(Dir, FVector::YAxisVector);
				}
				Tan.Normalize();
				Tangents.Add(FProcMeshTangent(Tan, false));
			}
		}

		auto VertAt = [&](int32 I, int32 J) -> int32
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
				const int32 A = VertAt(I, J);
				const int32 B = VertAt(I + 1, J);
				const int32 C = VertAt(I, J + 1);
				const int32 D = VertAt(I + 1, J + 1);
				if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
				{
					continue;
				}
				Indices.Add(A); Indices.Add(B); Indices.Add(C);
				Indices.Add(B); Indices.Add(D); Indices.Add(C);
			}
		}

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
			if (FVector::DotProduct(N, Positions[V].GetSafeNormal()) < 0.0f)
			{
				N = -N;
			}
			Normals[V] = N;
		}

		if (Positions.Num() >= 3 && Indices.Num() >= 3)
		{
			Comp->CreateMeshSection_LinearColor(Sections, Positions, Indices, Normals, UV0, Colors, Tangents, false);
			if (Material)
			{
				Comp->SetMaterial(Sections, Material);
			}
			TotalVerts += Positions.Num();
			++Sections;
		}
	}

	Comp->SetVisibility(Sections > 0);
	Comp->SetHiddenInGame(false);
	Comp->UpdateBounds();
	GX_PERF(1, TEXT("GX-editpatch sections=%d verts=%d"), Sections, TotalVerts);
}

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
	const TArray<FVector4>* EditHolesLocalM,
	TFunction<float(const FVector&)> DensityAt)
{
	if (!Owner || Rings.Num() == 0)
	{
		return;
	}

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
		if (UProceduralMeshComponent* Patch = EditPatch.Get())
		{
			BuildEditPatch(Patch, Stamp, PatchMaterial ? PatchMaterial : NearLit, EditHolesLocalM);
		}
		for (FRing& Ring : Rings)
		{
			// Ring 0 only. Punching the 8 m ring hid a huge low-res disk.
			if (Ring.CellM <= 3.0f)
			{
				ApplyRingEdits(Ring, EditHolesLocalM);
			}
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
		const float RebuildM = (I == 0) ? 70.0f : 400.0f;
		if (bReady && Built >= 1 && I > 0)
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
			if (Ring.CellM <= 3.0f)
			{
				ApplyRingEdits(Ring, EditHolesLocalM);
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
