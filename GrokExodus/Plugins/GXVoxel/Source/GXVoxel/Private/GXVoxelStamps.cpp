// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelStamps.h"
#include "GXNoise.h"

FVector3f FGXSphereStamp::UnitDir(const FVector3d& P, double& OutR)
{
	OutR = FMath::Sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z);
	if (OutR < 1e-9)
	{
		return FVector3f(1, 0, 0);
	}
	const double Inv = 1.0 / OutR;
	return FVector3f(static_cast<float>(P.X * Inv), static_cast<float>(P.Y * Inv), static_cast<float>(P.Z * Inv));
}

float FGXSphereStamp::SampleHeightDisplacement(const FVector3f& UnitDir) const
{
	const float Ux = UnitDir.X;
	const float Uy = UnitDir.Y;
	const float Uz = UnitDir.Z;

	const float Continents = FGXNoise::FBm(
		Ux * Params.ContinentFreq, Uy * Params.ContinentFreq, Uz * Params.ContinentFreq,
		Params.Seed, 5, 2.0f, 0.5f);
	const float Mountains = FGXNoise::Ridged(
		Ux * Params.MountainFreq, Uy * Params.MountainFreq, Uz * Params.MountainFreq,
		Params.Seed + 7u, 4);
	const float Detail = FGXNoise::FBm(
		Ux * Params.DetailFreq, Uy * Params.DetailFreq, Uz * Params.DetailFreq,
		Params.Seed + 19u, 3, 2.0f, 0.5f);

	const float LandMask = FMath::Clamp((Continents + 0.15f) * 1.4f, 0.0f, 1.0f);
	const float Relief = LandMask * (0.55f + 0.45f * Mountains) + Detail * 0.08f * LandMask;
	return Relief * Params.MaxRelief + Params.SeaLevelBias;
}

float FGXSphereStamp::SampleScarCarveMeters(const FVector3f& UnitDir) const
{
	const float N = FGXNoise::FBm(
		UnitDir.X * Params.ScarFreq, UnitDir.Y * Params.ScarFreq, UnitDir.Z * Params.ScarFreq,
		Params.Seed + 99u, 4);
	const float T = (N - Params.ScarThreshold) / FMath::Max(1.0f - Params.ScarThreshold, 0.05f);
	const float S = FMath::Clamp(T, 0.0f, 1.0f);
	if (S <= 0.0f)
	{
		return 0.0f;
	}
	const float Bowl = FGXNoise::Ridged(
		UnitDir.X * Params.ScarFreq * 1.7f,
		UnitDir.Y * Params.ScarFreq * 1.7f,
		UnitDir.Z * Params.ScarFreq * 1.7f,
		Params.Seed + 140u, 2);
	return S * Params.ScarMaxDepth * (0.45f + 0.55f * Bowl);
}

float FGXSphereStamp::SampleMoisture(const FVector3f& UnitDir) const
{
	const float M = FGXNoise::FBm(
		UnitDir.X * Params.MoistureFreq, UnitDir.Y * Params.MoistureFreq, UnitDir.Z * Params.MoistureFreq,
		Params.Seed + 33u, 4);
	return FMath::Clamp(M * 0.5f + 0.5f, 0.0f, 1.0f);
}

float FGXSphereStamp::SampleDensity(const FVector3d& PlanetLocalM) const
{
	double R = 0.0;
	const FVector3f Dir = UnitDir(PlanetLocalM, R);
	if (R < 1e-9)
	{
		return Params.Radius;
	}
	const float SurfaceR = Params.Radius + SampleHeightDisplacement(Dir) - SampleScarCarveMeters(Dir);
	return static_cast<float>(static_cast<double>(SurfaceR) - R);
}

int32 FGXSphereStamp::SampleMaterial(const FVector3d& PlanetLocalM, float Density) const
{
	if (Density <= 0.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::Air);
	}

	double R = 0.0;
	const FVector3f Dir = UnitDir(PlanetLocalM, R);
	if (R < 1e-9)
	{
		return static_cast<int32>(EGXVoxelMaterial::BedrockDeep);
	}

	const float BaseDisp = SampleHeightDisplacement(Dir);
	const float ScarCarve = SampleScarCarveMeters(Dir);
	const float SurfaceR = Params.Radius + BaseDisp - ScarCarve;
	const float Depth = SurfaceR - static_cast<float>(R);
	const float Latitude = FMath::Abs(Dir.Z);
	const float Moisture = SampleMoisture(Dir);
	const float HeightAboveBase = BaseDisp - ScarCarve;

	if (Depth > Params.CrustDepth * 3.5f)
	{
		return static_cast<int32>(EGXVoxelMaterial::BedrockDeep);
	}

	if (Depth > Params.CrustDepth * 0.35f && Depth < Params.CrustDepth * 2.8f)
	{
		const float OreN = FGXNoise::FBm(
			Dir.X * Params.OreFreq + Depth * 0.15f,
			Dir.Y * Params.OreFreq,
			Dir.Z * Params.OreFreq + static_cast<float>(R) * 0.02f,
			Params.Seed + 201u, 4);
		if (OreN > Params.OreThreshold)
		{
			if (Latitude > 0.55f || Moisture < 0.35f)
			{
				return static_cast<int32>(EGXVoxelMaterial::OreIron);
			}
			if (Moisture > 0.65f)
			{
				return static_cast<int32>(EGXVoxelMaterial::OreCrystal);
			}
			return static_cast<int32>(EGXVoxelMaterial::OreCopper);
		}
	}

	if (Depth > Params.CrustDepth)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	if (ScarCarve > 2.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::VolcanicScorched);
	}
	if (Latitude > 0.78f || HeightAboveBase > Params.MaxRelief * 0.72f)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}
	if (HeightAboveBase < Params.MaxRelief * 0.05f)
	{
		return Moisture > 0.55f
			? static_cast<int32>(EGXVoxelMaterial::WetMud)
			: static_cast<int32>(EGXVoxelMaterial::SandCoastal);
	}
	if (HeightAboveBase > Params.MaxRelief * 0.35f)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	if (Moisture < 0.40f)
	{
		return static_cast<int32>(EGXVoxelMaterial::DryDirt);
	}
	return static_cast<int32>(EGXVoxelMaterial::TemperateGrass);
}

FGXVoxelPacked FGXSphereStamp::SamplePacked(const FVector3d& PlanetLocalM) const
{
	const float D = SampleDensity(PlanetLocalM);
	if (D <= 0.0f)
	{
		return FGXVoxelPacked::MakeAir();
	}
	uint8 Flags = 0;
	double R = 0.0;
	const FVector3f Dir = UnitDir(PlanetLocalM, R);
	if (SampleScarCarveMeters(Dir) > 1.0f)
	{
		Flags |= EGXVoxelFlags::Scarred;
	}
	const int32 Mat = SampleMaterial(PlanetLocalM, D);
	if (Mat == static_cast<int32>(EGXVoxelMaterial::OreIron)
		|| Mat == static_cast<int32>(EGXVoxelMaterial::OreCopper)
		|| Mat == static_cast<int32>(EGXVoxelMaterial::OreCrystal))
	{
		Flags |= EGXVoxelFlags::OreVein;
	}
	return FGXVoxelPacked::FromDensity(D, static_cast<uint8>(Mat), Flags);
}
