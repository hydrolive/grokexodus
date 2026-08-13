// Copyright Grok Exodus. All Rights Reserved.
// Procedural stamp stack. Earth and Moon are parameter sets, not hardcoded ifs.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelTypes.h"

struct FGXPlanetStampParams
{
	float Radius = 60000.0f;
	float MaxRelief = 180.0f;
	float CrustDepth = 12.0f;
	float VoxelSize = 1.0f;
	uint32 Seed = 1337u;

	float ContinentFreq = 2.5f;
	float MountainFreq = 8.0f;
	float DetailFreq = 24.0f;
	float SeaLevelBias = 0.0f;
	float MoistureFreq = 3.5f;
	float OreFreq = 18.0f;
	float ScarFreq = 9.0f;
	float ScarMaxDepth = 28.0f;
	float ScarThreshold = 0.78f;
	float OreThreshold = 0.72f;

	/** Defaults matching the current Earth-like prototype mapper (4 km). */
	static FGXPlanetStampParams LegacyPrototype()
	{
		FGXPlanetStampParams P;
		P.Radius = 4000.0f;
		P.MaxRelief = 180.0f;
		P.CrustDepth = 12.0f;
		P.Seed = 1337u;
		return P;
	}

	static FGXPlanetStampParams Earth()
	{
		FGXPlanetStampParams P;
		P.Radius = 60000.0f;
		P.MaxRelief = 420.0f;
		P.CrustDepth = 24.0f;
		P.Seed = 1337u;
		return P;
	}

	static FGXPlanetStampParams Moon()
	{
		FGXPlanetStampParams P;
		P.Radius = 16000.0f;
		P.MaxRelief = 280.0f;
		P.CrustDepth = 18.0f;
		P.Seed = 9001u;
		P.ScarThreshold = 0.55f;
		P.ScarMaxDepth = 80.0f;
		P.OreThreshold = 0.68f;
		P.SeaLevelBias = 0.0f;
		return P;
	}
};

/**
 * Radial SDF + biome/ore/scar materials.
 * Sample functions are pure and safe to call from workers.
 */
class GXVOXEL_API FGXSphereStamp
{
public:
	explicit FGXSphereStamp(const FGXPlanetStampParams& InParams = FGXPlanetStampParams::LegacyPrototype())
		: Params(InParams)
	{
	}

	const FGXPlanetStampParams& GetParams() const { return Params; }
	void SetParams(const FGXPlanetStampParams& InParams) { Params = InParams; }

	float SampleHeightDisplacement(const FVector3f& UnitDir) const;
	float SampleScarCarveMeters(const FVector3f& UnitDir) const;
	float SampleMoisture(const FVector3f& UnitDir) const;
	float SampleDensity(const FVector3d& PlanetLocalM) const;
	int32 SampleMaterial(const FVector3d& PlanetLocalM, float Density) const;
	FGXVoxelPacked SamplePacked(const FVector3d& PlanetLocalM) const;

	static FVector3f UnitDir(const FVector3d& P, double& OutR);

private:
	FGXPlanetStampParams Params;
};
