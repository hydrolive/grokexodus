// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

/** Legacy brush spheres (save v3). Occupancy is the UV square. */
struct FGXEditSphere
{
	FVector C = FVector::ZeroVector;
	float R = 0.0f;
};

/**
 * One growing square on the cube-sphere face grid.
 * Landscape tiles punch that rectangle; marching cubes owns the hole.
 * Digs that reach the edge expand the square 2 m (snapped to 1 m cells).
 * Must match FGXCrustTiles face axes (including the 29° walk-grid rotate).
 */
struct FGXEditIsland
{
	static constexpr float CollarM = 2.0f;
	static constexpr float CellM = 1.0f;
	static constexpr float MarginM = 2.0f;
	static constexpr float MinHalfM = 6.0f;
	static constexpr float MaxExtentM = 48.0f;
	static constexpr int32 MaxSpheres = 64;

	TArray<FGXEditSphere> Spheres;
	int8 PatchFace = -1;
	float PatchU0 = 0.0f;
	float PatchU1 = 0.0f;
	float PatchV0 = 0.0f;
	float PatchV1 = 0.0f;

	bool HasPatch() const
	{
		return PatchFace >= 0 && PatchU1 > PatchU0 + 0.5f && PatchV1 > PatchV0 + 0.5f;
	}
	bool IsEmpty() const { return !HasPatch() && Spheres.Num() == 0; }
	void Reset();
	bool Contains(const FVector& P) const;
	bool ContainsPadded(const FVector& P, float PadM) const;
	bool OverlapsBox(const FBox& Box) const;
	void Add(const FVector& Center, float RadiusM);
	FBox Bounds() const;
	bool LooksValid(float PlanetRadiusM, float MaxReliefM) const;
	void Serialize(FArchive& Ar);
	FString DebugString() const;

	static int8 FaceOf(const FVector& Dir);
	static void FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB);
	void ProjectUV(const FVector& P, float& OutU, float& OutV) const;

private:
	void GrowPatch(const FVector& Center, float RadiusM);
};
