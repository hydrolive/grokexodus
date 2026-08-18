// Copyright Grok Exodus. All Rights Reserved.

#include "GXEphemeris.h"

FGXEphemeris FGXEphemeris::PlayableEarth()
{
	FGXEphemeris E;
	E.PlanetRadius = 60000.0;
	E.PlanetMu = 9.81 * E.PlanetRadius * E.PlanetRadius;
	E.EarthRot.SiderealPeriod = 1440.0;
	E.EarthRot.ObliquityRad = FMath::DegreesToRadians(23.0);
	E.EarthRot.PrimeMeridianAtEpoch = 0.0;
	E.EarthRot.Epoch = 0.0;

	// 365.25 scaled days. SMA is a visual AU (map / sun direction only).
	const double Year = 365.25 * E.EarthRot.SiderealPeriod;
	E.EarthHelio.SemiMajorAxis = 1.5e8;
	E.EarthHelio.Eccentricity = 0.0167;
	E.EarthHelio.Inclination = FMath::DegreesToRadians(0.0);
	E.EarthHelio.Epoch = 0.0;
	// MA=π → Earth on −X, sun inertial +X. Tilt is about X so +X spawn is noon.
	E.EarthHelio.MeanAnomaly0 = PI;
	E.SolMu = 4.0 * PI * PI * FMath::Pow(E.EarthHelio.SemiMajorAxis, 3.0) / (Year * Year);
	E.EarthHelio.Mu = E.SolMu;

	E.MoonEci.SemiMajorAxis = 280000.0;
	E.MoonEci.Eccentricity = 0.055;
	E.MoonEci.Inclination = FMath::DegreesToRadians(5.1);
	E.MoonEci.ArgPeriapsis = 0.0;
	E.MoonEci.LongAscNode = FMath::DegreesToRadians(40.0);
	E.MoonEci.MeanAnomaly0 = 1.2;
	E.MoonEci.Epoch = 0.0;
	E.MoonEci.Mu = E.PlanetMu;

	E.Atmosphere.bEnabled = true;
	E.Atmosphere.HeightMeters = 18000.0;
	E.Atmosphere.SeaLevelDensity = 1.225;
	E.Atmosphere.ScaleHeight = 4200.0;
	E.Atmosphere.RotationOmega = E.EarthRot.Omega();
	return E;
}

FQuat4d FGXEphemeris::InertialToBody(double UniversalTime) const
{
	return FGXBodyFrame::InertialToBody(EarthRot, UniversalTime);
}

FVector3d FGXEphemeris::BodyOmegaInertial(double UniversalTime) const
{
	(void)UniversalTime;
	const FQuat4d Tilt(FVector3d(1, 0, 0), -EarthRot.ObliquityRad);
	return Tilt.Inverse().RotateVector(FVector3d(0, 0, EarthRot.Omega()));
}

FVector3d FGXEphemeris::SunInertialDir(double UniversalTime) const
{
	const FGXOrbitalState S = FGXKepler::Evaluate(EarthHelio, UniversalTime);
	const double Mag = S.Position.Size();
	if (Mag < 1.0)
	{
		return FVector3d(1, 0, 0);
	}
	return S.Position * (-1.0 / Mag);
}

FVector3d FGXEphemeris::SunBodyDir(double UniversalTime) const
{
	return InertialToBody(UniversalTime).RotateVector(SunInertialDir(UniversalTime));
}

FVector3d FGXEphemeris::MoonInertialPos(double UniversalTime) const
{
	return FGXKepler::Evaluate(MoonEci, UniversalTime).Position;
}

FVector3d FGXEphemeris::MoonBodyPos(double UniversalTime) const
{
	return InertialToBody(UniversalTime).RotateVector(MoonInertialPos(UniversalTime));
}

double FGXEphemeris::MoonAngularRadius() const
{
	return 16000.0 / FMath::Max(MoonEci.SemiMajorAxis, 1.0);
}

FVector3d FGXEphemeris::NorthInertial() const
{
	const FQuat4d Tilt(FVector3d(1, 0, 0), -EarthRot.ObliquityRad);
	return Tilt.Inverse().RotateVector(FVector3d(0, 0, 1));
}

double FGXEphemeris::SolarDeclination(double UniversalTime) const
{
	const FVector3d S = SunInertialDir(UniversalTime);
	const FVector3d N = NorthInertial();
	return FMath::Asin(FMath::Clamp(FVector3d::DotProduct(S, N), -1.0, 1.0));
}

FString FGXEphemeris::SeasonName(double UniversalTime) const
{
	const double Dec = FMath::RadiansToDegrees(SolarDeclination(UniversalTime));
	const double Year = YearSeconds();
	const double Phase = FMath::Fmod(UniversalTime, Year) / Year;
	if (Dec > 12.0)
	{
		return TEXT("summer");
	}
	if (Dec < -12.0)
	{
		return TEXT("winter");
	}
	return (Phase < 0.5) ? TEXT("spring") : TEXT("autumn");
}

double FGXEphemeris::SeasonStartUT(int32 SeasonIndex) const
{
	const int32 I = ((SeasonIndex % 4) + 4) % 4;
	return YearSeconds() * (static_cast<double>(I) * 0.25);
}
