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

	/** Refine every coarse quad that touches this disk (metres). */
	float EditRefineM(float Rad, float CellM)
	{
		return Rad + CellM + 0.5f;
	}

	bool PointNearEdit(const FVector& Guess, const TArray<FVector4>* Edits, float CellM)
	{
		if (!Edits)
		{
			return false;
		}
		for (const FVector4& E : *Edits)
		{
			const float Rad = FMath::Abs(E.W);
			if (Rad < 0.05f)
			{
				continue;
			}
			const float R = EditRefineM(Rad, CellM);
			if (FVector::DistSquared(Guess, FVector(E.X, E.Y, E.Z)) < R * R)
			{
				return true;
			}
		}
		return false;
	}

	/** W<0 bowl, W>0 cap. Union, not sum — stacked digs bored to the core. */
	float ApplyEditSpheres(float SurfR, const FVector& Guess, const TArray<FVector4>* Edits)
	{
		if (!Edits || Edits->Num() == 0)
		{
			return SurfR;
		}
		float Sub = 0.0f;
		float Add = 0.0f;
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
			if (E.W < 0.0f)
			{
				Sub = FMath::Max(Sub, Drop);
			}
			else
			{
				Add = FMath::Max(Add, Drop);
			}
		}
		// Never pull a vert through the planet (0.7.46 #4 teal core).
		return FMath::Max(SurfR - Sub + Add, SurfR * 0.5f);
	}

	/** Subdivide one coarse quad so the crater welds to the landscape. */
	void EmitRefinedQuad(
		const FVector& PA, const FVector& PB, const FVector& PC, const FVector& PD,
		const FVector2D& UVA, const FVector2D& UVB, const FVector2D& UVC, const FVector2D& UVD,
		const FLinearColor& CA, const FLinearColor& CB, const FLinearColor& CC, const FLinearColor& CD,
		int32 Sub,
		const TArray<FVector4>* Edits,
		TArray<FVector>& Positions,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UV0,
		TArray<FLinearColor>& Colors,
		TArray<FProcMeshTangent>& Tangents,
		TArray<int32>& Indices)
	{
		Sub = FMath::Clamp(Sub, 2, 16);
		const int32 Dim = Sub + 1;
		const int32 Base = Positions.Num();
		const int32 FirstTri = Indices.Num();
		for (int32 J = 0; J < Dim; ++J)
		{
			const float V = static_cast<float>(J) / static_cast<float>(Sub);
			for (int32 I = 0; I < Dim; ++I)
			{
				const float U = static_cast<float>(I) / static_cast<float>(Sub);
				const FVector P0 = FMath::Lerp(PA, PB, U);
				const FVector P1 = FMath::Lerp(PC, PD, U);
				FVector P = FMath::Lerp(P0, P1, V);
				FVector Dir = P.GetSafeNormal();
				if (Dir.IsNearlyZero())
				{
					Dir = FVector(1, 0, 0);
				}
				float Surf = P.Size() * 0.01f;
				Surf = ApplyEditSpheres(Surf, Dir * Surf, Edits);
				P = Dir * Surf * 100.0f;
				Positions.Add(P);
				Normals.Add(Dir);
				UV0.Add(FMath::Lerp(FMath::Lerp(UVA, UVB, U), FMath::Lerp(UVC, UVD, U), V));
				Colors.Add(FMath::Lerp(FMath::Lerp(CA, CB, U), FMath::Lerp(CC, CD, U), V));
				FVector Tan = FVector::CrossProduct(Dir, FVector::ZAxisVector);
				if (Tan.SizeSquared() < 1e-6f)
				{
					Tan = FVector::CrossProduct(Dir, FVector::YAxisVector);
				}
				Tan.Normalize();
				Tangents.Add(FProcMeshTangent(Tan, false));
			}
		}
		for (int32 J = 0; J < Sub; ++J)
		{
			for (int32 I = 0; I < Sub; ++I)
			{
				const int32 A = Base + I + J * Dim;
				const int32 B = A + 1;
				const int32 C = A + Dim;
				const int32 D = C + 1;
				Indices.Add(A); Indices.Add(B); Indices.Add(C);
				Indices.Add(B); Indices.Add(D); Indices.Add(C);
			}
		}
		TArray<FVector> AccN;
		AccN.Init(FVector::ZeroVector, Dim * Dim);
		for (int32 T0 = FirstTri; T0 + 2 < Indices.Num(); T0 += 3)
		{
			const int32 IA = Indices[T0] - Base;
			const int32 IB = Indices[T0 + 1] - Base;
			const int32 IC = Indices[T0 + 2] - Base;
			const FVector FN = FVector::CrossProduct(
				Positions[Base + IB] - Positions[Base + IA],
				Positions[Base + IC] - Positions[Base + IA]);
			AccN[IA] += FN; AccN[IB] += FN; AccN[IC] += FN;
		}
		for (int32 V = 0; V < AccN.Num(); ++V)
		{
			FVector N = AccN[V].GetSafeNormal();
			if (N.IsNearlyZero())
			{
				N = Normals[Base + V];
			}
			if (FVector::DotProduct(N, Positions[Base + V].GetSafeNormal()) < 0.0f)
			{
				N = -N;
			}
			Normals[Base + V] = N;
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
	// Ring 0 is the walk surface we punch. Async cook delayed the hole
	// and left the old lid up (the 0.7.43 shell).
	if (Rings.Num() > 0)
	{
		if (UProceduralMeshComponent* C = Rings[0].Comp.Get())
		{
			C->bUseAsyncCooking = false;
		}
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GXHorizonClipmap: %d rings (edits stitch into ring 0)"), Rings.Num());
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
	Ring.StampIndices = Indices;
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
	const TArray<FVector4>* Edits)
{
	UProceduralMeshComponent* Comp = Ring.Comp.Get();
	if (!Comp || Ring.StampPos.Num() == 0 || Ring.StampIndices.Num() < 3)
	{
		return;
	}
	(void)Stamp;

	TArray<FVector> Positions = Ring.StampPos;
	TArray<FVector> Normals = Ring.LiveN;
	TArray<FVector2D> UV0 = Ring.UV0;
	TArray<FLinearColor> Colors = Ring.Colors;
	TArray<FProcMeshTangent> Tangents = Ring.Tangents;
	TArray<int32> Indices;

	int32 Refined = 0;
	const bool bHaveEdits = Edits && Edits->Num() > 0;
	const bool bHaveGrid = Ring.GridDim >= 2 && Ring.GridOf.Num() == Ring.GridDim * Ring.GridDim;
	if (bHaveEdits && bHaveGrid)
	{
		auto GuessOf = [&](int32 VI) -> FVector
		{
			if (!Ring.StampDir.IsValidIndex(VI) || !Ring.StampSurfM.IsValidIndex(VI))
			{
				return FVector::ZeroVector;
			}
			return Ring.StampDir[VI] * Ring.StampSurfM[VI];
		};

		const int32 Dim = Ring.GridDim;
		const int32 QW = Dim - 1;
		const int32 Sub = FMath::Clamp(FMath::RoundToInt(Ring.CellM / 0.22f), 2, 16);
		auto Grid = [&](int32 I, int32 J) -> int32
		{
			return Ring.GridOf[I + J * Dim];
		};

		TArray<uint8> Mark;
		Mark.Init(0, QW * QW);
		for (int32 J = 0; J < QW; ++J)
		{
			for (int32 I = 0; I < QW; ++I)
			{
				const int32 A = Grid(I, J);
				const int32 B = Grid(I + 1, J);
				const int32 C = Grid(I, J + 1);
				const int32 D = Grid(I + 1, J + 1);
				if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
				{
					continue;
				}
				const FVector GA = GuessOf(A);
				const FVector GB = GuessOf(B);
				const FVector GC = GuessOf(C);
				const FVector GD = GuessOf(D);
				const FVector Mid = (GA + GB + GC + GD) * 0.25f;
				if (PointNearEdit(GA, Edits, Ring.CellM)
					|| PointNearEdit(GB, Edits, Ring.CellM)
					|| PointNearEdit(GC, Edits, Ring.CellM)
					|| PointNearEdit(GD, Edits, Ring.CellM)
					|| PointNearEdit(Mid, Edits, Ring.CellM))
				{
					Mark[I + J * QW] = 1;
				}
			}
		}
		// One-cell skirt so a carved quad never shares an edge with an
		// unrefined neighbour (that crack was the 2 m window to the core).
		TArray<uint8> Dilated = Mark;
		for (int32 J = 0; J < QW; ++J)
		{
			for (int32 I = 0; I < QW; ++I)
			{
				if (!Mark[I + J * QW])
				{
					continue;
				}
				for (int32 DJ = -1; DJ <= 1; ++DJ)
				{
					for (int32 DI = -1; DI <= 1; ++DI)
					{
						const int32 NI = I + DI;
						const int32 NJ = J + DJ;
						if (NI >= 0 && NJ >= 0 && NI < QW && NJ < QW)
						{
							Dilated[NI + NJ * QW] = 1;
						}
					}
				}
			}
		}

		for (int32 J = 0; J < QW; ++J)
		{
			for (int32 I = 0; I < QW; ++I)
			{
				const int32 A = Grid(I, J);
				const int32 B = Grid(I + 1, J);
				const int32 C = Grid(I, J + 1);
				const int32 D = Grid(I + 1, J + 1);
				if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
				{
					continue;
				}
				if (Dilated[I + J * QW])
				{
					EmitRefinedQuad(
						Positions[A], Positions[B], Positions[C], Positions[D],
						UV0[A], UV0[B], UV0[C], UV0[D],
						Colors[A], Colors[B], Colors[C], Colors[D],
						Sub, Edits,
						Positions, Normals, UV0, Colors, Tangents, Indices);
					++Refined;
				}
				else
				{
					Indices.Add(A); Indices.Add(B); Indices.Add(C);
					Indices.Add(B); Indices.Add(D); Indices.Add(C);
				}
			}
		}
	}
	else
	{
		Indices = Ring.StampIndices;
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
	GX_PERF(1, TEXT("GX-ring0 refine quads=%d tris=%d verts=%d"),
		Refined, Indices.Num() / 3, Positions.Num());
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
			// Ring 0 and 1. Spawn edits must still be there when you
			// walk away and look back (0.7.46 #3).
			if (Ring.CellM <= 10.0f)
			{
				ApplyRingEdits(Ring, Stamp, (Ring.CellM <= 3.0f) ? NearLit : FarLit, EditHolesLocalM);
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
			if (Ring.CellM <= 10.0f)
			{
				ApplyRingEdits(Ring, Stamp, UseMat, EditHolesLocalM);
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
