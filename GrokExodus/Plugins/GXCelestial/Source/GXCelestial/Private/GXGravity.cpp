// Copyright Grok Exodus. All Rights Reserved.

#include "GXGravity.h"
#include "GXMath.h"

FVector3d FGXGravity::Acceleration(const FVector3d& PositionFromBodyM, double Mu)
{
	const double R2 = PositionFromBodyM.SizeSquared();
	if (R2 < 1.0)
	{
		return FVector3d::ZeroVector;
	}
	const double InvR = 1.0 / FMath::Sqrt(R2);
	const double InvR2 = InvR * InvR;
	return PositionFromBodyM * (-Mu * InvR2 * InvR);
}

FVector3d FGXGravity::DirectionTowardCenter(const FVector3d& PositionFromBodyM)
{
	return GXSafeNormal(-PositionFromBodyM, FVector3d(0, 0, -1));
}

double FGXGravity::DynamicPressure(double DensityKgM3, double SpeedMS)
{
	return 0.5 * DensityKgM3 * SpeedMS * SpeedMS;
}

double FGXGravity::HeatFlux(double DensityKgM3, double SpeedMS, double K, double N)
{
	if (DensityKgM3 <= 0.0 || SpeedMS <= 0.0)
	{
		return 0.0;
	}
	return K * FMath::Pow(DensityKgM3, N) * SpeedMS * SpeedMS * SpeedMS;
}
