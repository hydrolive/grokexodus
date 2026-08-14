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

float FGXSphereStamp::SampleLegacyHeight(const FVector3f& UnitDir) const
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

float FGXSphereStamp::SampleMoonHeight(const FVector3f& UnitDir) const
{
	const float Ux = UnitDir.X;
	const float Uy = UnitDir.Y;
	const float Uz = UnitDir.Z;

	const float Highlands = FGXNoise::Ridged(
		Ux * Params.MountainFreq, Uy * Params.MountainFreq, Uz * Params.MountainFreq,
		Params.Seed + 7u, 5);
	const float Mare = FGXNoise::FBm(
		Ux * Params.ContinentFreq, Uy * Params.ContinentFreq, Uz * Params.ContinentFreq,
		Params.Seed, 4, 2.0f, 0.5f);
	const float MareMask = FGXNoise::Smooth01((-Mare - 0.05f) / 0.28f);
	const float Detail = FGXNoise::FBm(
		Ux * Params.DetailFreq, Uy * Params.DetailFreq, Uz * Params.DetailFreq,
		Params.Seed + 19u, 3, 2.0f, 0.5f);
	const float Local = FGXNoise::Ridged(
		Ux * 180.0f, Uy * 180.0f, Uz * 180.0f, Params.Seed + 70u, 3);

	const float H = 0.22f
		+ 0.48f * Highlands * (1.0f - MareMask)
		+ 0.04f * MareMask
		+ 0.08f * Local
		+ 0.04f * Detail;
	return H * Params.MaxRelief + Params.SeaLevelBias;
}

FGXEarthField FGXSphereStamp::SampleEarthField(const FVector3f& UnitDir, bool bNeedMoisture) const
{
	FGXEarthField Out;
	const float Ux = UnitDir.X;
	const float Uy = UnitDir.Y;
	const float Uz = UnitDir.Z;
	const float Lat = FMath::Abs(Uz);
	const float Relief = FMath::Max(Params.MaxRelief, 1.0f);

	float PlateF1 = 0.0f;
	float PlateF2 = 0.0f;
	FGXNoise::WorleyF1F2(
		Ux * Params.PlateFreq, Uy * Params.PlateFreq, Uz * Params.PlateFreq,
		Params.Seed + 3u, PlateF1, PlateF2);
	// F2-F1 is ~0 on a suture and large in a cell interior. Do not hash-by-cell:
	// that stamped each plate as a mesa with vertical walls.
	const float Suture = FMath::Clamp(1.0f - (PlateF2 - PlateF1) * 1.8f, 0.0f, 1.0f);
	const float Belt = Suture * Suture;

	float Continents = FGXNoise::FBm(
		Ux * Params.ContinentFreq, Uy * Params.ContinentFreq, Uz * Params.ContinentFreq,
		Params.Seed, 3, 2.0f, 0.55f);
	const float SpawnLand = FGXNoise::Smooth01((Ux - 0.08f) / 0.72f);
	Continents += SpawnLand * 0.40f;

	// Wide coastal ramp — land emerges from sea level, it does not cliff out of the abyss.
	const float LandMask = FGXNoise::Smooth01((Continents + 0.12f) / 0.70f);
	const float OceanMask = 1.0f - LandMask;
	const float Coast = 4.0f * LandMask * OceanMask;
	Out.LandMask = LandMask;

	const float Highland = 0.5f + 0.5f * FGXNoise::FBm(
		Ux * Params.PlateauFreq, Uy * Params.PlateauFreq, Uz * Params.PlateauFreq,
		Params.Seed + 21u, 3, 2.0f, 0.5f);
	const float Mountains = FGXNoise::Ridged(
		Ux * Params.MountainFreq, Uy * Params.MountainFreq, Uz * Params.MountainFreq,
		Params.Seed + 7u, 3);
	// Continuous range: slow highland envelope * soft ridge. No saturate-to-flat.
	const float RangeBody = Highland * Mountains;
	const float Orogeny = LandMask * (0.10f * Highland + 0.20f * RangeBody + 0.10f * Belt * RangeBody);
	Out.Orogeny = Orogeny;

	const float Foothills = LandMask * Highland * FGXNoise::FBm(
		Ux * Params.MountainFreq * 0.55f, Uy * Params.MountainFreq * 0.55f, Uz * Params.MountainFreq * 0.55f,
		Params.Seed + 8u, 3, 2.0f, 0.5f) * 0.05f;

	const float Hills = LandMask * FGXNoise::FBm(
		Ux * Params.HillFreq, Uy * Params.HillFreq, Uz * Params.HillFreq,
		Params.Seed + 17u, 3, 2.0f, 0.5f) * 0.04f;

	const float Shield = LandMask * (1.0f - Highland) * 0.03f;
	const float Plateau = LandMask * Highland * 0.06f;

	const float Wx = FGXNoise::FBm(
		Ux * Params.RiverFreq * 0.32f, Uy * Params.RiverFreq * 0.32f, Uz * Params.RiverFreq * 0.32f,
		Params.Seed + 40u, 3, 2.0f, 0.5f) * 0.16f;
	const float Wy = FGXNoise::FBm(
		Ux * Params.RiverFreq * 0.32f + 17.0f, Uy * Params.RiverFreq * 0.32f, Uz * Params.RiverFreq * 0.32f,
		Params.Seed + 41u, 3, 2.0f, 0.5f) * 0.16f;
	const float Rivers = FGXNoise::Ridged(
		Ux * Params.RiverFreq + Wx, Uy * Params.RiverFreq + Wy, Uz * Params.RiverFreq,
		Params.Seed + 42u, 4);
	Out.RiverCarve = LandMask * (1.0f - Out.Orogeny * 0.70f) * FMath::Pow(Rivers, 4.2f) * Params.ValleyAmp;

	const float Can = FGXNoise::Ridged(
		Ux * Params.CanyonFreq, Uy * Params.CanyonFreq, Uz * Params.CanyonFreq,
		Params.Seed + 55u, 3);
	const float CanyonGate = FGXNoise::Smooth01((FGXNoise::FBm(
		Ux * 4.2f, Uy * 4.2f, Uz * 4.2f, Params.Seed + 56u, 3, 2.0f, 0.5f) - 0.32f) / 0.28f);
	Out.CanyonCarve = LandMask * CanyonGate * FMath::Pow(Can, 4.4f) * Params.CanyonAmp;

	const float Rift = Belt * (1.0f - Highland) * LandMask * 0.03f;

	const float LocalRidge = LandMask * FGXNoise::FBm(
		Ux * Params.LocalRidgeFreq, Uy * Params.LocalRidgeFreq, Uz * Params.LocalRidgeFreq,
		Params.Seed + 70u, 3, 2.0f, 0.5f);
	const float LocalGully = LandMask * FGXNoise::Ridged(
		Ux * Params.LocalGullyFreq, Uy * Params.LocalGullyFreq, Uz * Params.LocalGullyFreq,
		Params.Seed + 71u, 2);
	const float Local = LocalRidge * 0.018f - FMath::Pow(LocalGully, 3.4f) * 0.012f;

	float VF1 = 0.0f;
	float VF2 = 0.0f;
	FGXNoise::WorleyF1F2(
		Ux * Params.VolcanoFreq, Uy * Params.VolcanoFreq, Uz * Params.VolcanoFreq,
		Params.Seed + 80u, VF1, VF2);
	const int32 VX = FMath::FloorToInt(Ux * Params.VolcanoFreq);
	const int32 VY = FMath::FloorToInt(Uy * Params.VolcanoFreq);
	const int32 VZ = FMath::FloorToInt(Uz * Params.VolcanoFreq);
	const float VGate = FGXNoise::HashToFloat(FGXNoise::Hash(VX, VY, VZ, Params.Seed + 81u));
	Out.Volcano = 0.0f;
	if (VGate > 0.885f && LandMask > 0.35f)
	{
		const float ConeR = 0.11f;
		const float Cone = FMath::Max(0.0f, 1.0f - VF1 / ConeR);
		const float Caldera = FMath::Max(0.0f, 1.0f - VF1 / (ConeR * 0.22f)) * 0.34f;
		Out.Volcano = FMath::Pow(Cone, 1.55f) * 0.40f - Caldera;
	}

	const float Alpine = FGXNoise::Smooth01((Out.Orogeny - 0.32f) / 0.28f);
	const float Polar = FGXNoise::Smooth01((Lat - 0.60f) / 0.24f);
	const float Glacial = (Polar * 0.75f + Alpine * Polar) * FGXNoise::Ridged(
		Ux * 18.0f, Uy * 18.0f, Uz * 18.0f, Params.Seed + 90u, 3);
	const float GlacialCarve = FMath::Pow(Glacial, 2.4f) * 0.045f * LandMask;

	const float Detail = FGXNoise::FBm(
		Ux * Params.DetailFreq, Uy * Params.DetailFreq, Uz * Params.DetailFreq,
		Params.Seed + 19u, 3, 2.0f, 0.5f) * 0.010f * LandMask;

	const float Abyssal = -Params.OceanDepthFrac * (0.35f + 0.25f * (0.5f + 0.5f * FGXNoise::FBm(
		Ux * 1.55f, Uy * 1.55f, Uz * 1.55f, Params.Seed + 5u, 3, 2.0f, 0.5f)));
	const float Trench = OceanMask * Belt * 0.35f * Params.TrenchAmp;
	// Shelf stays near sea level. Deep ocean only far from land.
	const float OceanFloor = Abyssal * FMath::Pow(OceanMask, 2.0f) - Trench;
	const float Shelf = Coast * 0.02f;

	// Inland weight ramps after the beach so coasts are beaches, not 1 km cliffs.
	const float Inland = FGXNoise::Smooth01((LandMask - 0.35f) / 0.50f);
	float LandH = 0.03f * LandMask
		+ Inland * (Shield + Hills + Foothills + Plateau + Orogeny
			+ Out.Volcano * 0.35f + Local + Detail
			- Out.RiverCarve - Out.CanyonCarve - Rift - GlacialCarve);

	const float NormH = FMath::Lerp(OceanFloor + Shelf, LandH, LandMask);
	Out.HeightM = NormH * Relief + Params.SeaLevelBias;
	Out.SlopeProxy = FMath::Clamp(
		Out.Orogeny * 1.35f + Out.CanyonCarve * 2.8f + FMath::Abs(LocalRidge) * 0.20f + FMath::Abs(Out.Volcano) * 1.1f,
		0.0f, 1.0f);

	if (bNeedMoisture)
	{
		const float M = FGXNoise::FBm(
			Ux * Params.MoistureFreq, Uy * Params.MoistureFreq, Uz * Params.MoistureFreq,
			Params.Seed + 33u, 4);
		const float CoastalWet = Coast * 0.25f;
		const float RiverWet = Out.RiverCarve * 2.2f;
		Out.Moisture = FMath::Clamp(M * 0.5f + 0.5f + CoastalWet + RiverWet - Out.Orogeny * 0.2f, 0.0f, 1.0f);
	}
	return Out;
}

float FGXSphereStamp::SampleHeightDisplacement(const FVector3f& UnitDir) const
{
	switch (Params.Profile)
	{
	case EGXStampProfile::Earth:
		return SampleEarthField(UnitDir, false).HeightM;
	case EGXStampProfile::Moon:
		return SampleMoonHeight(UnitDir);
	default:
		return SampleLegacyHeight(UnitDir);
	}
}

float FGXSphereStamp::SampleSurfaceRadius(const FVector3f& UnitDir) const
{
	return Params.Radius + SampleHeightDisplacement(UnitDir) - SampleScarCarveMeters(UnitDir);
}

float FGXSphereStamp::SampleSlopeDegrees(const FVector3f& UnitDir, float OffsetMeters) const
{
	FVector Up(UnitDir.X, UnitDir.Y, UnitDir.Z);
	FVector T, B;
	Up.FindBestAxisVectors(T, B);
	const float Arc = OffsetMeters / FMath::Max(Params.Radius, 100.0f);
	const FVector3f A = (UnitDir + FVector3f(T.X, T.Y, T.Z) * Arc).GetSafeNormal();
	const FVector3f C = (UnitDir + FVector3f(B.X, B.Y, B.Z) * Arc).GetSafeNormal();
	const float H0 = SampleHeightDisplacement(UnitDir);
	const float HA = SampleHeightDisplacement(A);
	const float HB = SampleHeightDisplacement(C);
	const float G = FMath::Sqrt(FMath::Square(HA - H0) + FMath::Square(HB - H0)) / FMath::Max(OffsetMeters, 0.5f);
	return FMath::RadiansToDegrees(FMath::Atan(G));
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
	if (Params.Profile == EGXStampProfile::Earth)
	{
		return SampleEarthField(UnitDir, true).Moisture;
	}
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

int32 FGXSphereStamp::SampleEarthMaterial(const FVector3d& PlanetLocalM, float Density) const
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

	const FGXEarthField Field = SampleEarthField(Dir, true);
	const float ScarCarve = SampleScarCarveMeters(Dir);
	const float SurfaceR = Params.Radius + Field.HeightM - ScarCarve;
	const float Depth = SurfaceR - static_cast<float>(R);
	const float Latitude = FMath::Abs(Dir.Z);
	const float HeightAboveSea = Field.HeightM - ScarCarve;
	const float Relief = FMath::Max(Params.MaxRelief, 1.0f);

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
			if (Latitude > 0.55f || Field.Moisture < 0.35f)
			{
				return static_cast<int32>(EGXVoxelMaterial::OreIron);
			}
			if (Field.Moisture > 0.65f)
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
	if (Field.Volcano > 0.05f || ScarCarve > 4.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::VolcanicScorched);
	}

	const float SnowLine = Relief * (0.58f - Latitude * 0.32f);
	if (Latitude > 0.78f || HeightAboveSea > SnowLine)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}
	if (Field.LandMask < 0.55f || HeightAboveSea < Relief * 0.045f)
	{
		if (Field.RiverCarve > 0.03f || Field.Moisture > 0.62f)
		{
			return static_cast<int32>(EGXVoxelMaterial::WetMud);
		}
		return static_cast<int32>(EGXVoxelMaterial::SandCoastal);
	}
	if (Field.SlopeProxy > 0.38f || Field.CanyonCarve > 0.04f || HeightAboveSea > Relief * 0.38f)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	if (Field.Moisture < 0.38f)
	{
		return static_cast<int32>(EGXVoxelMaterial::DryDirt);
	}
	return static_cast<int32>(EGXVoxelMaterial::TemperateGrass);
}

int32 FGXSphereStamp::SampleMaterial(const FVector3d& PlanetLocalM, float Density) const
{
	if (Params.Profile == EGXStampProfile::Earth)
	{
		return SampleEarthMaterial(PlanetLocalM, Density);
	}

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
