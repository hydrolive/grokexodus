// Copyright Grok Exodus. All Rights Reserved.
// Double-precision helpers. Prefer FVector3d / FQuat4d from the engine.
#pragma once

#include "CoreMinimal.h"

/** Meters (authoritative sim) ↔ Unreal centimeters. */
namespace GXUnits
{
	inline constexpr double MetersToCm = 100.0;
	inline constexpr double CmToMeters = 0.01;

	inline FVector3d CmToMeters3(const FVector& Cm)
	{
		return FVector3d(Cm.X, Cm.Y, Cm.Z) * CmToMeters;
	}

	inline FVector MetersToCm3(const FVector3d& M)
	{
		return FVector(
			static_cast<float>(M.X * MetersToCm),
			static_cast<float>(M.Y * MetersToCm),
			static_cast<float>(M.Z * MetersToCm));
	}
}

/** Safe normalize that returns Fallback when near zero. */
inline FVector3d GXSafeNormal(const FVector3d& V, const FVector3d& Fallback = FVector3d(0, 0, -1))
{
	const double S = V.SizeSquared();
	if (S < 1e-24)
	{
		return Fallback;
	}
	return V * (1.0 / FMath::Sqrt(S));
}

inline FVector GXSafeNormal3f(const FVector& V, const FVector& Fallback = FVector(0, 0, -1))
{
	const float S = V.SizeSquared();
	if (S < 1e-12f)
	{
		return Fallback;
	}
	return V * FMath::InvSqrt(S);
}

/** Rotate a double vector by a double quaternion (engine FQuat4d). */
inline FVector3d GXRotate(const FQuat4d& Q, const FVector3d& V)
{
	return Q.RotateVector(V);
}
