// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"
#include "Templates/Function.h"

/** Legacy brush spheres (save v3). Occupancy is the walk-grid cell mask. */
struct FGXEditSphere
{
	FVector C = FVector::ZeroVector;
	float R = 0.0f;
};

/**
 * Discrete hole map on the cube-sphere walk grid (same 29° axes + stagger
 * as FGXCrustTiles). A cell is excavated if stamp-surface density is air;
 * occupancy is that set dilated by 1 cell (collar). No 12–48 m rectangle.
 */
struct FGXEditIsland
{
	static constexpr float CollarM = 2.0f;
	static constexpr float CellM = 1.0f;
	static constexpr float MarginM = 2.0f;
	static constexpr float MinHalfM = 6.0f;
	static constexpr float MaxExtentM = 48.0f;
	static constexpr int32 MaxSpheres = 64;
	static constexpr int32 MaxCells = 2048;

	TArray<FGXEditSphere> Spheres;
	int8 PatchFace = -1;
	float PatchU0 = 0.0f;
	float PatchU1 = 0.0f;
	float PatchV0 = 0.0f;
	float PatchV1 = 0.0f;

	/** Walk cells we own (excavated + 1-cell dilation). X=face, Y=i, Z=j. */
	TSet<FIntVector> Mask;
	TSet<FIntVector> Excavated;

	bool HasMask() const { return Mask.Num() > 0; }
	bool HasPatch() const
	{
		return HasMask()
			|| (PatchFace >= 0 && PatchU1 > PatchU0 + 0.5f && PatchV1 > PatchV0 + 0.5f);
	}
	bool IsEmpty() const { return !HasPatch() && Spheres.Num() == 0; }
	void Reset();
	bool Contains(const FVector& P) const;
	bool ContainsPadded(const FVector& P, float PadM) const;
	bool ContainsCell(const FIntVector& Cell) const;
	bool IsExcavated(const FIntVector& Cell) const;
	bool OverlapsBox(const FBox& Box) const;
	void Add(const FVector& Center, float RadiusM);
	void MarkBrush(
		const FVector& Center,
		float RadiusM,
		TFunctionRef<float(const FVector&)> DensityAt,
		TFunctionRef<float(const FVector3f&)> StampRadiusAt);
	void MarkExcavatedWorld(const FVector& P);
	FBox Bounds() const;
	bool LooksValid(float PlanetRadiusM, float MaxReliefM) const;
	void Serialize(FArchive& Ar);
	FString DebugString() const;

	static int8 FaceOf(const FVector& Dir);
	static void FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB);
	void ProjectUV(const FVector& P, float& OutU, float& OutV) const;
	FIntVector WalkCellOf(const FVector& P) const;
	FVector CellStampCenter(const FIntVector& Cell, float Mag, float StampR) const;

private:
	void GrowPatch(const FVector& Center, float RadiusM);
	void DilateNew(const TArray<FIntVector>& NewExc);
	void RefreshPatchBounds(float Mag);
};
