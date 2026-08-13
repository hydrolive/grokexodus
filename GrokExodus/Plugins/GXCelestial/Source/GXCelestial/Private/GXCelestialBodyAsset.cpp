// Copyright Grok Exodus. All Rights Reserved.

#include "GXCelestialBodyAsset.h"

FGXAtmosphereModel UGXCelestialBodyAsset::MakeAtmosphere() const
{
	FGXAtmosphereModel A;
	A.bEnabled = bHasAtmosphere;
	A.HeightMeters = AtmosphereHeightMeters;
	A.SeaLevelDensity = SeaLevelDensity;
	A.ScaleHeight = ScaleHeightMeters;
	A.RotationOmega = (SiderealDaySeconds > 1e-6) ? (2.0 * PI / SiderealDaySeconds) : 0.0;
	return A;
}

FGXBodyRotation UGXCelestialBodyAsset::MakeRotation() const
{
	FGXBodyRotation R;
	R.SiderealPeriod = SiderealDaySeconds;
	R.ObliquityRad = FMath::DegreesToRadians(ObliquityDeg);
	return R;
}

FGXKeplerElements UGXCelestialBodyAsset::MakeOrbitElements(double ParentMu) const
{
	FGXKeplerElements E;
	E.SemiMajorAxis = FMath::Max(OrbitSemiMajorMeters, 1.0);
	E.Eccentricity = OrbitEccentricity;
	E.Inclination = FMath::DegreesToRadians(OrbitInclinationDeg);
	E.Mu = ParentMu;
	return E;
}

void UGXCelestialBodyAsset::ApplyEarthDefaults(UGXCelestialBodyAsset* Asset)
{
	if (!Asset)
	{
		return;
	}
	Asset->BodyId = TEXT("Earth");
	Asset->ParentBodyId = TEXT("Sol");
	Asset->RadiusMeters = 60000.0;
	Asset->SurfaceG = 9.81;
	Asset->MassKg = 5.972e16; // scaled with R³-ish for SOI, not real mass
	Asset->OrbitSemiMajorMeters = 1.5e8; // visual-scale AU (map only)
	Asset->SiderealDaySeconds = 1440.0;
	Asset->ObliquityDeg = 23.0;
	Asset->bHasAtmosphere = true;
	Asset->AtmosphereHeightMeters = 18000.0;
	Asset->SeaLevelDensity = 1.225;
	Asset->ScaleHeightMeters = 4200.0;
}

void UGXCelestialBodyAsset::ApplyMoonDefaults(UGXCelestialBodyAsset* Asset)
{
	if (!Asset)
	{
		return;
	}
	Asset->BodyId = TEXT("Moon");
	Asset->ParentBodyId = TEXT("Earth");
	Asset->RadiusMeters = 16000.0;
	Asset->SurfaceG = 1.62;
	Asset->MassKg = 7.35e14;
	Asset->OrbitSemiMajorMeters = 280000.0;
	Asset->OrbitInclinationDeg = 5.1;
	Asset->SiderealDaySeconds = 1440.0 * 27.3; // tidally locked-ish at scaled day
	Asset->ObliquityDeg = 6.7;
	Asset->bHasAtmosphere = false;
	Asset->AtmosphereHeightMeters = 0.0;
	Asset->SeaLevelDensity = 0.0;
}

UGXCelestialBodyAsset* UGXCelestialBodyAsset::MakeEarthDefaults()
{
	return nullptr; // author as a data asset in content; defaults via ApplyEarthDefaults
}
