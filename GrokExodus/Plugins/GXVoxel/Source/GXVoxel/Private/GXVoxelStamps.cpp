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

	// Earth-like basins (~70% ocean) with a wide coastal ramp. +X spawn
	// stays land via SpawnLand. Ice/sand are materials, not deeper voxels.
	const float LandMask = FGXNoise::Smooth01((Continents - 0.20f) / 0.58f);
	const float OceanMask = 1.0f - LandMask;
	const float Coast = 4.0f * LandMask * OceanMask;
	Out.LandMask = LandMask;

	// Most land is plains. Mountains are the high tail of a 2-octave field
	// so flanks are kilometres wide (no 80° walls).
	float Domain = 0.5f + 0.5f * FGXNoise::FBm(
		Ux * Params.PlateauFreq, Uy * Params.PlateauFreq, Uz * Params.PlateauFreq,
		Params.Seed + 21u, 2, 2.0f, 0.5f);
	// 500 m pad, then rolling hills. Ranges are elongated spines 8–10 km out
	// so flanks start past ~4 km (voxel stream never meshes a 2 km cliff).
	const float ArcM = FMath::Acos(FMath::Clamp(Ux, -1.0f, 1.0f)) * Params.Radius;
	const float Basin = FGXNoise::Smooth01((500.0f - ArcM) / 200.0f);
	const float R = FMath::Max(Params.Radius, 1.0f);
	const FVector3f Here(Ux, Uy, Uz);
	// Weight is 1 on the crest. The old (HalfWid-Dist)/Flank peaked at
	// Smooth01(HalfWid/Flank)≈0.28, so "mountains" were 30 m bumps.
	auto RangeW = [&](FVector3f Mid, FVector3f Along, float HalfLen, float HalfWid, float Flank, bool bPlateau) -> float
	{
		Mid = Mid.GetSafeNormal();
		Along = (Along - Mid * FVector3f::DotProduct(Along, Mid)).GetSafeNormal();
		if (Along.SizeSquared() < 1e-6f)
		{
			return 0.0f;
		}
		const FVector3f Across = FVector3f::CrossProduct(Mid, Along).GetSafeNormal();
		const float AlongM = FVector3f::DotProduct(Here - Mid * FVector3f::DotProduct(Here, Mid), Along) * R;
		const float AcrossM = FVector3f::DotProduct(Here - Mid * FVector3f::DotProduct(Here, Mid), Across) * R;
		const float Se = AlongM - FMath::Clamp(AlongM, -HalfLen, HalfLen);
		const float Dist = FMath::Sqrt(Se * Se + AcrossM * AcrossM);
		const float Fall = FMath::Max(Flank, 1.0f);
		if (bPlateau)
		{
			// 1 inside HalfWid, 0 at HalfWid+Flank — foothill apron.
			return FGXNoise::Smooth01((HalfWid + Fall - Dist) / Fall);
		}
		// Triangular ridge: 1 on the centerline, 0 at HalfWid+Flank.
		const float T = FMath::Clamp(1.0f - Dist / FMath::Max(HalfWid + Fall, 1.0f), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	};
	auto MidAt = [&](float EastM, float NorthM) -> FVector3f
	{
		return FVector3f(1.0f, EastM / R, NorthM / R).GetSafeNormal();
	};
	const FVector3f AZ(0, 0, 1);
	const FVector3f AY(0, 1, 0);
	// Crests stay past ~4 km so the voxel stream never meshes a cliff.
	// More POIs around the compass so left/right/front/behind all have a range.
	struct FSpine { float East; float North; float HalfLen; float HalfWid; float Flank; };
	const FSpine Spines[] = {
		{ 8200.0f, 500.0f, 5600.0f, 480.0f, 2800.0f },
		{ 10800.0f, 2600.0f, 4200.0f, 420.0f, 2400.0f },
		{ -1800.0f, 8800.0f, 5000.0f, 460.0f, 2600.0f },
		{ -8200.0f, -2600.0f, 4600.0f, 440.0f, 2500.0f },
		{ 400.0f, -8600.0f, 4200.0f, 400.0f, 2300.0f },
		{ 6800.0f, -6400.0f, 3800.0f, 380.0f, 2200.0f },
		{ 5600.0f, 7400.0f, 4000.0f, 400.0f, 2300.0f },
		{ -6200.0f, -7200.0f, 3900.0f, 380.0f, 2200.0f },
		{ -7400.0f, 5800.0f, 4100.0f, 400.0f, 2300.0f },
		{ 4600.0f, 2800.0f, 2800.0f, 340.0f, 1800.0f },
		{ 4000.0f, -3600.0f, 2600.0f, 320.0f, 1700.0f },
		{ -2400.0f, 5000.0f, 2700.0f, 330.0f, 1750.0f },
		{ -3200.0f, -4800.0f, 2600.0f, 320.0f, 1700.0f },
	};
	float Spine = 0.0f;
	float Feet = 0.0f;
	float Rise = 0.0f;
	for (const FSpine& S : Spines)
	{
		const FVector3f Mid = MidAt(S.East, S.North);
		const FVector3f Along = (FMath::Abs(S.East) >= FMath::Abs(S.North)) ? AZ : AY;
		Spine = FMath::Max(Spine, RangeW(Mid, Along, S.HalfLen, S.HalfWid, S.Flank, false));
		Feet = FMath::Max(Feet, RangeW(Mid, Along, S.HalfLen * 1.15f, S.HalfWid * 4.0f, S.Flank * 1.85f, true));
		Rise = FMath::Max(Rise, RangeW(Mid, Along, S.HalfLen * 1.25f, S.HalfWid * 3.4f, S.Flank * 2.7f, true));
	}
	Domain = FMath::Lerp(Domain, 0.22f, Basin * 0.80f);
	// Near the voxel stream, cap Domain so we do not mesh a 2 km wall.
	// Far land (orbit) must still have ranges — spines only cover ~11 km of +X.
	const float NearStream = FGXNoise::Smooth01((4200.0f - ArcM) / 1600.0f);
	Domain = FMath::Min(Domain, FMath::Lerp(0.90f, 0.44f, NearStream));
	const float Away = 1.0f - Basin;
	Domain = FMath::Max(Domain, FMath::Lerp(Domain, 0.52f, Rise * Away));
	Domain = FMath::Max(Domain, FMath::Lerp(Domain, 0.64f, Feet * Away));
	Domain = FMath::Max(Domain, FMath::Lerp(Domain, 0.93f, Spine * Away));
	const float PlainsW = 1.0f - FGXNoise::Smooth01((Domain - 0.34f) / 0.26f);
	const float MountainW = FGXNoise::Smooth01((Domain - 0.54f) / 0.26f);
	const float HillW = FMath::Clamp(1.0f - PlainsW - MountainW, 0.0f, 1.0f);
	const float Highland = Domain;

	const float Mass = 0.58f + 0.42f * FGXNoise::FBm(
		Ux * Params.MountainFreq, Uy * Params.MountainFreq, Uz * Params.MountainFreq,
		Params.Seed + 7u, 2, 2.0f, 0.5f);
	const float PlainsH = 0.028f;
	const float HillH = PlainsH + 0.022f * (0.35f + 0.65f * FGXNoise::FBm(
		Ux * Params.HillFreq * 0.42f, Uy * Params.HillFreq, Uz * Params.HillFreq * 1.18f,
		Params.Seed + 17u, 3, 2.0f, 0.52f));
	const float Ridge = FGXNoise::Ridged(
		Ux * Params.MountainFreq * 2.4f, Uy * Params.MountainFreq * 2.4f, Uz * Params.MountainFreq * 2.4f,
		Params.Seed + 9u, 3);
	// Cols cut the crest so the range is peaks and passes, not a fence.
	const float Cols = 0.42f + 0.58f * (0.5f + 0.5f * FGXNoise::FBm(
		Ux * 3.8f, Uy * 3.8f, Uz * 3.8f, Params.Seed + 11u, 2, 2.0f, 0.5f));
	// Jagged skyline at a scale the 80 m clipmap can draw (not a smooth cone).
	const float Skyline = FGXNoise::Ridged(
		Ux * 26.0f, Uy * 26.0f, Uz * 26.0f, Params.Seed + 12u, 2);
	// Elongated summit on the east crest — a ridge node, not a radial volcano.
	const float Summit = RangeW(MidAt(8100.0f, 1100.0f), AZ, 1600.0f, 380.0f, 1500.0f, false);
	const float Summit2 = RangeW(MidAt(9000.0f, -400.0f), AZ, 1100.0f, 320.0f, 1300.0f, false);
	const float Summit3 = RangeW(MidAt(400.0f, -8400.0f), AY, 1200.0f, 300.0f, 1200.0f, false);
	const float Summit4 = RangeW(MidAt(-7200.0f, 5600.0f), AZ, 1100.0f, 300.0f, 1200.0f, false);
	const float PeakH = PlainsH
		+ (0.24f + 0.28f * Ridge) * Mass * MountainW * Cols
		+ MountainW * Skyline * 0.14f
		+ Summit * (0.18f + 0.10f * Ridge)
		+ Summit2 * (0.12f + 0.08f * Ridge)
		+ Summit3 * (0.14f + 0.08f * Ridge)
		+ Summit4 * (0.13f + 0.08f * Ridge);
	Out.Volcano = FMath::Max3(Summit, Summit2, FMath::Max(Summit3, Summit4)) * (0.35f + 0.45f * Ridge);
	const float Orogeny = LandMask * (PlainsW * PlainsH + HillW * HillH + MountainW * PeakH);
	Out.Orogeny = LandMask * MountainW * Mass;

	const float Foothills = LandMask * (HillW + MountainW * 0.35f) * FGXNoise::FBm(
		Ux * Params.MountainFreq * 0.45f, Uy * Params.MountainFreq * 0.45f, Uz * Params.MountainFreq * 0.45f,
		Params.Seed + 8u, 3, 2.0f, 0.5f) * 0.016f;

	// Anisotropic ridges, not circular blobs. Gate so the 280 m pad stays walkable.
	const float HillGate = 1.0f - FGXNoise::Smooth01((280.0f - ArcM) / 160.0f);
	const float HillN = FGXNoise::FBm(
		Ux * Params.HillFreq,
		Uy * Params.HillFreq * 0.42f,
		Uz * Params.HillFreq * 1.18f,
		Params.Seed + 18u, 4, 2.0f, 0.52f);
	const float Hills = LandMask * HillGate * 0.024f * (0.30f + 0.70f * HillN);
	const float Shield = LandMask * PlainsW * 0.004f;
	const float Plateau = 0.0f;

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
	const float Local = LocalRidge * 0.008f - FMath::Pow(FMath::Abs(LocalGully), 3.8f) * 0.004f;

	const float Alpine = FGXNoise::Smooth01((Out.Orogeny - 0.32f) / 0.28f);
	const float Polar = FGXNoise::Smooth01((Lat - 0.60f) / 0.24f);
	const float Glacial = (Polar * 0.75f + Alpine * Polar) * FGXNoise::Ridged(
		Ux * 18.0f, Uy * 18.0f, Uz * 18.0f, Params.Seed + 90u, 3);
	const float GlacialCarve = FMath::Pow(Glacial, 2.4f) * 0.045f * LandMask;

	const float Detail = FGXNoise::FBm(
		Ux * Params.DetailFreq, Uy * Params.DetailFreq, Uz * Params.DetailFreq,
		Params.Seed + 19u, 3, 2.0f, 0.5f) * 0.0035f * LandMask;

	const float Abyssal = -Params.OceanDepthFrac * (0.35f + 0.25f * (0.5f + 0.5f * FGXNoise::FBm(
		Ux * 1.55f, Uy * 1.55f, Uz * 1.55f, Params.Seed + 5u, 3, 2.0f, 0.5f)));
	const float Trench = OceanMask * Belt * 0.35f * Params.TrenchAmp;
	// Shelf stays near sea level. Deep ocean only far from land.
	const float OceanFloor = Abyssal * FMath::Pow(OceanMask, 2.0f) - Trench;
	const float Shelf = Coast * 0.02f;

	// Inland weight ramps after the beach so coasts are beaches, not 1 km cliffs.
	const float Inland = FGXNoise::Smooth01((LandMask - 0.35f) / 0.50f);
	const float DetailScale = PlainsW * 0.40f + HillW * 0.70f + MountainW;
	// Plate-suture ranges on every continent (not only +X spines).
	const float WorldBelt = LandMask * Belt * (0.10f + 0.22f * Mass * Ridge)
		* (1.0f - NearStream * 0.55f);
	float LandH = 0.01f * LandMask
		+ Inland * (Shield + Hills + Foothills + Plateau + Orogeny + WorldBelt
			+ Local * 0.45f * DetailScale
			+ Detail * DetailScale
			- Out.RiverCarve * PlainsW * 0.35f
			- Out.CanyonCarve * (HillW + MountainW)
			- Rift - GlacialCarve * MountainW);
	Out.Orogeny = FMath::Max(Out.Orogeny, WorldBelt);

	const float NormH = FMath::Lerp(OceanFloor + Shelf, LandH, LandMask);
	Out.HeightM = NormH * Relief + Params.SeaLevelBias;
	Out.SlopeProxy = FMath::Clamp(
		MountainW * 0.70f + Out.Volcano * 0.85f + Out.CanyonCarve * 2.8f
			+ FMath::Abs(LocalRidge) * 0.20f,
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
	if (ScarCarve > 4.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::VolcanicScorched);
	}

	// POI volcano: scorched flanks, snow cap. Do this before the grass/sand
	// tests — 0.7.16 stamped the whole cone as grass hills.
	if (Field.Volcano > 0.18f)
	{
		if (HeightAboveSea > 1800.0f)
		{
			return static_cast<int32>(EGXVoxelMaterial::SnowIce);
		}
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}

	const float SnowLine = Relief * (0.48f - Latitude * 0.28f);
	if (Latitude > 0.78f || HeightAboveSea > SnowLine)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}

	if (Field.LandMask < 0.22f || HeightAboveSea < -12.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}
	if (Field.LandMask < 0.40f && HeightAboveSea < 90.0f)
	{
		if (Field.RiverCarve > 0.03f || Field.Moisture > 0.62f)
		{
			return static_cast<int32>(EGXVoxelMaterial::WetMud);
		}
		return static_cast<int32>(EGXVoxelMaterial::SandCoastal);
	}

	// Rock on mountain *sides* (slope) and real ranges. Not flat hilltops.
	if (Field.SlopeProxy > 0.16f || Field.Orogeny > 0.15f || Field.CanyonCarve > 0.08f
		|| Field.Volcano > 0.18f)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	if (Field.Moisture < 0.22f)
	{
		return static_cast<int32>(EGXVoxelMaterial::DryDirt);
	}
	return static_cast<int32>(EGXVoxelMaterial::TemperateGrass);
}

int32 FGXSphereStamp::SampleSurfaceMaterial(const FVector3f& UnitDir) const
{
	const FVector3f Dir = UnitDir.GetSafeNormal();
	const FGXEarthField Field = SampleEarthField(Dir, true);
	const float Height = Field.HeightM;
	const float Lat = FMath::Abs(Dir.Z);
	const float Relief = FMath::Max(Params.MaxRelief, 1.0f);
	const float SnowLine = Relief * (0.48f - Lat * 0.28f);

	// Ice oceans. Thin sand only on the beach — 0.13.0 treated most
	// land as sand (LandMask 0.28–0.54 + Height<55) so orbit was one tan sheet.
	if (Field.LandMask < 0.22f || Height < -12.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}
	if (Lat > 0.80f || Height > SnowLine)
	{
		return static_cast<int32>(EGXVoxelMaterial::SnowIce);
	}
	if (Field.LandMask < 0.40f && Height < 90.0f)
	{
		return static_cast<int32>(EGXVoxelMaterial::SandCoastal);
	}
	if (Field.Volcano > 0.18f)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	// Rock on mountain sides; dirt along the foot.
	if (Field.SlopeProxy > 0.15f || Field.Orogeny > 0.16f || Field.CanyonCarve > 0.08f)
	{
		return static_cast<int32>(EGXVoxelMaterial::RockyCliff);
	}
	if (Field.SlopeProxy > 0.07f || (Field.Orogeny > 0.05f && Height < Relief * 0.22f))
	{
		return static_cast<int32>(EGXVoxelMaterial::DryDirt);
	}
	// Mud breaks up continuous grass (rivers + wet noise).
	const float MudN = FGXNoise::FBm(
		Dir.X * 18.0f, Dir.Y * 18.0f, Dir.Z * 18.0f, Params.Seed + 91u, 3);
	if (Field.RiverCarve > 0.02f || Field.Moisture > 0.72f || MudN > 0.42f)
	{
		return static_cast<int32>(EGXVoxelMaterial::WetMud);
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
