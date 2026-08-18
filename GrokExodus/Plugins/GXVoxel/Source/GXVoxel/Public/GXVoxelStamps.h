// Copyright Grok Exodus. All Rights Reserved.
// Procedural stamp stack. Earth and Moon are parameter sets, not hardcoded ifs.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelTypes.h"

enum class EGXStampProfile : uint8
{
	/** Bit-identical to the original 4 km continent+ridge mapper (tests). */
	Legacy = 0,
	/** Layered Earth geomorphology: plates, ranges, valleys, coasts, volcanoes. */
	Earth = 1,
	/** Airless highlands, maria, and impact scars. */
	Moon = 2,
};

struct FGXPlanetStampParams
{
	EGXStampProfile Profile = EGXStampProfile::Legacy;

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

	/** Unit-sphere frequencies for the Earth profile. Wavelength ≈ 2πR / f. */
	float PlateFreq = 3.6f;
	float HillFreq = 28.0f;
	float RiverFreq = 48.0f;
	float CanyonFreq = 22.0f;
	float PlateauFreq = 6.5f;
	float LocalRidgeFreq = 320.0f;
	float LocalGullyFreq = 720.0f;
	float VolcanoFreq = 16.0f;

	/** Fractions of MaxRelief. */
	float ValleyAmp = 0.20f;
	float CanyonAmp = 0.16f;
	float OceanDepthFrac = 0.30f;
	float TrenchAmp = 0.50f;

	/** Stable id for crust cache / atlas files. Changes when the planet would look different. */
	uint64 Fingerprint() const;

	/** Defaults matching the current Earth-like prototype mapper (4 km). */
	static FGXPlanetStampParams LegacyPrototype()
	{
		FGXPlanetStampParams P;
		P.Profile = EGXStampProfile::Legacy;
		P.Radius = 4000.0f;
		P.MaxRelief = 180.0f;
		P.CrustDepth = 12.0f;
		P.Seed = 1337u;
		return P;
	}

	static FGXPlanetStampParams Earth()
	{
		FGXPlanetStampParams P;
		P.Profile = EGXStampProfile::Earth;
		P.Radius = 60000.0f;
		P.MaxRelief = 2400.0f;
		P.CrustDepth = 96.0f;
		P.Seed = 1337u;
		P.ContinentFreq = 1.35f;
		P.MountainFreq = 9.9f;    // fingerprint — 0.7.19 jagged range, no cone stack
		P.DetailFreq = 900.0f;
		P.MoistureFreq = 2.4f;
		P.ScarFreq = 4.5f;
		P.ScarMaxDepth = 80.0f;
		P.ScarThreshold = 0.88f;
		P.OreFreq = 14.0f;
		P.OreThreshold = 0.74f;
		P.SeaLevelBias = 0.0f;
		P.PlateFreq = 2.2f;
		P.HillFreq = 380.0f;      // ~1 km near hills, not 2 km-away rolls
		P.RiverFreq = 140.0f;
		P.CanyonFreq = 55.0f;
		P.PlateauFreq = 12.0f;   // ~31 km plains vs range domains
		P.LocalRidgeFreq = 700.0f;
		P.LocalGullyFreq = 1100.0f;
		P.VolcanoFreq = 8.0f;
		P.ValleyAmp = 0.04f;
		P.CanyonAmp = 0.03f;
		P.OceanDepthFrac = 0.30f;
		P.TrenchAmp = 0.50f;
		return P;
	}

	static FGXPlanetStampParams Moon()
	{
		FGXPlanetStampParams P;
		P.Profile = EGXStampProfile::Moon;
		P.Radius = 16000.0f;
		P.MaxRelief = 900.0f;
		P.CrustDepth = 48.0f;
		P.Seed = 9001u;
		P.ContinentFreq = 3.0f;
		P.MountainFreq = 10.0f;
		P.DetailFreq = 40.0f;
		P.ScarThreshold = 0.52f;
		P.ScarMaxDepth = 220.0f;
		P.OreThreshold = 0.68f;
		P.SeaLevelBias = 0.0f;
		return P;
	}
};

/** Cached Earth-field sample so height and material share one evaluation. */
struct FGXEarthField
{
	float HeightM = 0.0f;
	float LandMask = 0.0f;
	float Orogeny = 0.0f;
	float RiverCarve = 0.0f;
	float CanyonCarve = 0.0f;
	float Volcano = 0.0f;
	float Moisture = 0.0f;
	float SlopeProxy = 0.0f;
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
	float SampleSurfaceRadius(const FVector3f& UnitDir) const;
	float SampleScarCarveMeters(const FVector3f& UnitDir) const;
	float SampleMoisture(const FVector3f& UnitDir) const;
	float SampleDensity(const FVector3d& PlanetLocalM) const;
	int32 SampleMaterial(const FVector3d& PlanetLocalM, float Density) const;
	/** Surface biome id (1–7) from height / land / slope. No depth/ore. */
	int32 SampleSurfaceMaterial(const FVector3f& UnitDir) const;
	FGXVoxelPacked SamplePacked(const FVector3d& PlanetLocalM) const;
	FGXEarthField SampleEarthField(const FVector3f& UnitDir, bool bNeedMoisture) const;

	/** Approximate slope in degrees from a 3-point height stencil (foliage). */
	float SampleSlopeDegrees(const FVector3f& UnitDir, float OffsetMeters = 4.0f) const;

	static FVector3f UnitDir(const FVector3d& P, double& OutR);

private:
	float SampleLegacyHeight(const FVector3f& UnitDir) const;
	float SampleMoonHeight(const FVector3f& UnitDir) const;
	int32 SampleEarthMaterial(const FVector3d& PlanetLocalM, float Density) const;

	FGXPlanetStampParams Params;
};
