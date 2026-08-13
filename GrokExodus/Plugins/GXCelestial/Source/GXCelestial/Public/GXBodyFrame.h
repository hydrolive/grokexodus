// Copyright Grok Exodus. All Rights Reserved.
// ECI ↔ body-fixed. The UE planet actor never rotates.
#pragma once

#include "CoreMinimal.h"

struct FGXBodyRotation
{
	double SiderealPeriod = 1440.0; // seconds (scaled day; default 24 min)
	double ObliquityRad = 0.40142572796; // 23 deg
	double PrimeMeridianAtEpoch = 0.0;
	double Epoch = 0.0;

	double Omega() const
	{
		return (SiderealPeriod > 1e-6) ? (2.0 * PI / SiderealPeriod) : 0.0;
	}

	double Theta(double UniversalTime) const
	{
		return PrimeMeridianAtEpoch + Omega() * (UniversalTime - Epoch);
	}
};

class GXCELESTIAL_API FGXBodyFrame
{
public:
	/** Inertial → body-fixed rotation at UT. */
	static FQuat4d InertialToBody(const FGXBodyRotation& Rot, double UniversalTime);

	static FVector3d InertialToBodyPoint(const FQuat4d& R, const FVector3d& InertialM);
	static FVector3d BodyToInertialPoint(const FQuat4d& R, const FVector3d& BodyM);

	static FVector3d InertialVelocityToBody(
		const FQuat4d& R,
		const FVector3d& OmegaInertial,
		const FVector3d& RInertial,
		const FVector3d& VInertial);

	static FVector3d BodyVelocityToInertial(
		const FQuat4d& R,
		const FVector3d& OmegaInertial,
		const FVector3d& RInertial,
		const FVector3d& VBody);

	/** Round-trip error in meters. */
	static double PointRoundTripError(const FGXBodyRotation& Rot, double UT, const FVector3d& InertialM);
};
