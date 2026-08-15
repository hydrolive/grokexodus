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

	/** W<0 bowl, W>0 cap. Apply in stroke order so a nearby add cannot
	 *  cancel a dig (0.7.53 left a grass lid inside a ring). */
	float ApplyEditSpheres(float SurfR, const FVector& Guess, const TArray<FVector4>* Edits)
	{
		if (!Edits || Edits->Num() == 0)
		{
			return SurfR;
		}
		const float FloorR = SurfR * 0.5f;
		float S = SurfR;
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
				S -= Drop;
			}
			else
			{
				S += Drop;
			}
		}
		return FMath::Max(S, FloorR);
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
		// Full disk. A 120 m inner hole stayed at the old center after
		// ring 0 walked off — look-back was a teal rectangle (0.7.52).
		{ 0.0f, 560.0f, 8.0f, 2.0f },
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
	TArray<FVector4> HotEdits;
	if (Edits && Edits->Num() > 0)
	{
		// All strokes keep their height. Only the last 8 are tessellated
		// fine — older pits stay as 2 m dents instead of healing (0.7.56 #9).
		const int32 HotStart = FMath::Max(0, Edits->Num() - 8);
		for (int32 I = HotStart; I < Edits->Num(); ++I)
		{
			HotEdits.Add((*Edits)[I]);
		}
	}
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
		const int32 Sub = FMath::Clamp(FMath::RoundToInt(Ring.CellM / 0.22f), 6, 10);
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
				if (PointNearEdit(GA, &HotEdits, Ring.CellM)
					|| PointNearEdit(GB, &HotEdits, Ring.CellM)
					|| PointNearEdit(GC, &HotEdits, Ring.CellM)
					|| PointNearEdit(GD, &HotEdits, Ring.CellM)
					|| PointNearEdit(Mid, &HotEdits, Ring.CellM))
				{
					Mark[I + J * QW] = 1;
				}
			}
		}
		// One-cell skirt. Two cells plus 48 edits is what made the hitch.
		TArray<uint8> Dilated = Mark;
		for (int32 Pass = 0; Pass < 1; ++Pass)
		{
			TArray<uint8> Next = Dilated;
			for (int32 J = 0; J < QW; ++J)
			{
				for (int32 I = 0; I < QW; ++I)
				{
					if (!Dilated[I + J * QW])
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
								Next[NI + NJ * QW] = 1;
							}
						}
					}
				}
			}
			Dilated = MoveTemp(Next);
		}

		const int32 StampCount = Ring.StampPos.Num();
		TSet<int32> CornerCSG;
		auto CSGStampVert = [&](int32 VI)
		{
			if (VI == INDEX_NONE || CornerCSG.Contains(VI)
				|| !Ring.StampDir.IsValidIndex(VI) || !Ring.StampSurfM.IsValidIndex(VI))
			{
				return;
			}
			CornerCSG.Add(VI);
			const FVector Dir = Ring.StampDir[VI];
			const float Surf = Ring.StampSurfM[VI];
			const float NewS = ApplyEditSpheres(Surf, Dir * Surf, Edits);
			Positions[VI] = Dir * NewS * 100.0f;
		};

		for (int32 VI = 0; VI < Ring.StampPos.Num(); ++VI)
		{
			if (PointNearEdit(GuessOf(VI), Edits, Ring.CellM))
			{
				CSGStampVert(VI);
			}
		}

		TMap<int64, int32> FineOf;
		FineOf.Reserve(4096);
		auto FineKey = [](int32 FI, int32 FJ) -> int64
		{
			return (static_cast<int64>(static_cast<uint32>(FI)) << 32) | static_cast<uint32>(FJ);
		};
		auto MakeTan = [](const FVector& Dir) -> FProcMeshTangent
		{
			FVector Tan = FVector::CrossProduct(Dir, FVector::ZAxisVector);
			if (Tan.SizeSquared() < 1e-6f)
			{
				Tan = FVector::CrossProduct(Dir, FVector::YAxisVector);
			}
			Tan.Normalize();
			return FProcMeshTangent(Tan, false);
		};
		auto GetFine = [&](int32 I, int32 J, int32 SI, int32 SJ) -> int32
		{
			if (SI == 0 && SJ == 0) { return Grid(I, J); }
			if (SI == Sub && SJ == 0) { return Grid(I + 1, J); }
			if (SI == 0 && SJ == Sub) { return Grid(I, J + 1); }
			if (SI == Sub && SJ == Sub) { return Grid(I + 1, J + 1); }
			const int64 Key = FineKey(I * Sub + SI, J * Sub + SJ);
			if (const int32* Found = FineOf.Find(Key))
			{
				return *Found;
			}
			const int32 CA = Grid(I, J);
			const int32 CB = Grid(I + 1, J);
			const int32 CC = Grid(I, J + 1);
			const int32 CD = Grid(I + 1, J + 1);
			const float U = static_cast<float>(SI) / static_cast<float>(Sub);
			const float V = static_cast<float>(SJ) / static_cast<float>(Sub);
			// Bilinear of the *stamp* corners, then CSG once. Shared key so
			// adjacent quads cannot open a T-junction hole (0.7.49 #1/#2).
			FVector P = FMath::Lerp(
				FMath::Lerp(Ring.StampPos[CA], Ring.StampPos[CB], U),
				FMath::Lerp(Ring.StampPos[CC], Ring.StampPos[CD], U), V);
			FVector Dir = P.GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = Ring.StampDir[CA];
			}
			float Surf = P.Size() * 0.01f;
			Surf = ApplyEditSpheres(Surf, Dir * Surf, Edits);
			P = Dir * Surf * 100.0f;
			const int32 Idx = Positions.Num();
			Positions.Add(P);
			Normals.Add(Dir);
			UV0.Add(FMath::Lerp(FMath::Lerp(Ring.UV0[CA], Ring.UV0[CB], U), FMath::Lerp(Ring.UV0[CC], Ring.UV0[CD], U), V));
			Colors.Add(FMath::Lerp(FMath::Lerp(Ring.Colors[CA], Ring.Colors[CB], U), FMath::Lerp(Ring.Colors[CC], Ring.Colors[CD], U), V));
			Tangents.Add(MakeTan(Dir));
			FineOf.Add(Key, Idx);
			return Idx;
		};

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
				const FVector QuadMid = (GuessOf(A) + GuessOf(B) + GuessOf(C) + GuessOf(D)) * 0.25f;
				if (Ring.CellM > 3.0f && PointNearEdit(QuadMid, &HotEdits, Ring.CellM))
				{
					// Hot pit is the fine ring-0 bowl. Leave an 8 m hole
					// so a deep dig does not hit an uncut floor (#4).
					continue;
				}
				if (Ring.CellM <= 3.0f && Dilated[I + J * QW])
				{
					for (int32 SJ = 0; SJ < Sub; ++SJ)
					{
						for (int32 SI = 0; SI < Sub; ++SI)
						{
							const int32 FA = GetFine(I, J, SI, SJ);
							const int32 FB = GetFine(I, J, SI + 1, SJ);
							const int32 FC = GetFine(I, J, SI, SJ + 1);
							const int32 FD = GetFine(I, J, SI + 1, SJ + 1);
							Indices.Add(FA); Indices.Add(FB); Indices.Add(FC);
							Indices.Add(FB); Indices.Add(FD); Indices.Add(FC);
						}
					}
					++Refined;
				}
				else
				{
					Indices.Add(A); Indices.Add(B); Indices.Add(C);
					Indices.Add(B); Indices.Add(D); Indices.Add(C);
				}
			}
		}

		// New fine verts only. Recomputing stamp verts pulled crater-wall
		// slope onto neighbouring 8 m tris (0.7.49 #3 dirt smear).
		TArray<FVector> AccN;
		AccN.Init(FVector::ZeroVector, Positions.Num());
		for (int32 T0 = 0; T0 + 2 < Indices.Num(); T0 += 3)
		{
			const int32 IA = Indices[T0], IB = Indices[T0 + 1], IC = Indices[T0 + 2];
			if (IA < StampCount && IB < StampCount && IC < StampCount)
			{
				continue;
			}
			const FVector FN = FVector::CrossProduct(Positions[IB] - Positions[IA], Positions[IC] - Positions[IA]);
			if (IA >= StampCount) { AccN[IA] += FN; }
			if (IB >= StampCount) { AccN[IB] += FN; }
			if (IC >= StampCount) { AccN[IC] += FN; }
		}
		for (int32 V = StampCount; V < Positions.Num(); ++V)
		{
			FVector N = AccN[V].GetSafeNormal();
			if (N.IsNearlyZero())
			{
				N = Positions[V].GetSafeNormal();
			}
			// BuildRing winding's Cross points inward. Lighting must still
			// face the sun — 0.7.50 left these inward and the refine disk
			// went pitch black.
			const FVector Radial = Positions[V].GetSafeNormal();
			if (!Radial.IsNearlyZero() && FVector::DotProduct(N, Radial) < 0.0f)
			{
				N = -N;
			}
			Normals[V] = N;
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
		const float RebuildM = (I == 0) ? 70.0f : (I == 1) ? 180.0f : 400.0f;
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
