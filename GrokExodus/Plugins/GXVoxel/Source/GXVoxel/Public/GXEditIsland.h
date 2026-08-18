// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

/** Growing union of brush spheres. The voxel mesh owns this region. */
struct FGXEditSphere
{
	FVector C = FVector::ZeroVector;
	float R = 0.0f;
};

struct FGXEditIsland
{
	static constexpr float CollarM = 0.75f;
	static constexpr int32 MaxSpheres = 64;

	TArray<FGXEditSphere> Spheres;

	bool IsEmpty() const { return Spheres.Num() == 0; }
	void Reset() { Spheres.Reset(); }
	bool Contains(const FVector& P) const;
	bool OverlapsBox(const FBox& Box) const;
	void Add(const FVector& Center, float RadiusM);
	FBox Bounds() const;
	/** True when every sphere sits on the crust (rejects LWC 4-byte garbage). */
	bool LooksValid(float PlanetRadiusM, float MaxReliefM) const;
	void Serialize(FArchive& Ar);
};
