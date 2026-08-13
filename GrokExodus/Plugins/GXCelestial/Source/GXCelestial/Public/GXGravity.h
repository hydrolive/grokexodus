// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FGXAtmosphereModel
{
	bool bEnabled = true;
	double HeightMeters = 18000.0;
	double SeaLevelDensity = 1.225;   // kg/m^3
	double ScaleHeight = 4200.0;      // m
	double RotationOmega = 0.0;       // rad/s about body Z (sidereal)

	float DensityAt(double AltitudeMeters) const
	{
		if (!bEnabled || AltitudeMeters >= HeightMeters)
		{
			return 0.0f;
		}
		const double H = FMath::Max(ScaleHeight, 1.0);
		return static_cast<float>(SeaLevelDensity * FMath::Exp(-AltitudeMeters / H));
	}
};

class GXCELESTIAL_API FGXGravity
{
public:
	/** Inverse-square acceleration (m/s^2) toward origin. μ in m^3/s^2. */
	static FVector3d Acceleration(const FVector3d& PositionFromBodyM, double Mu);

	static double SurfaceMu(double SurfaceG, double RadiusMeters)
	{
		return SurfaceG * RadiusMeters * RadiusMeters;
	}

	static FVector3d DirectionTowardCenter(const FVector3d& PositionFromBodyM);

	/** Dynamic pressure q = 0.5 ρ v_rel² */
	static double DynamicPressure(double DensityKgM3, double SpeedMS);

	/** Stagnation-ish heating ∝ ρ^n v³ */
	static double HeatFlux(double DensityKgM3, double SpeedMS, double K = 1.0e-4, double N = 0.5);
};
