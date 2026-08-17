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
	static constexpr float CollarM = 2.0f;
	static constexpr int32 MaxSpheres = 64;

	TArray<FGXEditSphere> Spheres;

	bool IsEmpty() const { return Spheres.Num() == 0; }
	void Reset() { Spheres.Reset(); }
	bool Contains(const FVector& P) const;
	bool OverlapsBox(const FBox& Box) const;
	void Add(const FVector& Center, float RadiusM);
	FBox Bounds() const;
	void Serialize(FArchive& Ar);
};
