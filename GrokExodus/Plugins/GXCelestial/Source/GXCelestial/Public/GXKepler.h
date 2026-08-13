// Copyright Grok Exodus. All Rights Reserved.
// Patched-conic Kepler. Pure functions — safe on workers.
#pragma once

#include "CoreMinimal.h"

struct FGXKeplerElements
{
	double SemiMajorAxis = 1.0;     // meters
	double Eccentricity = 0.0;
	double Inclination = 0.0;       // rad
	double LongAscNode = 0.0;       // Ω rad
	double ArgPeriapsis = 0.0;      // ω rad
	double MeanAnomaly0 = 0.0;      // rad at epoch
	double Epoch = 0.0;             // UT seconds
	double Mu = 3.5316e10;          // m^3/s^2
};

struct FGXOrbitalState
{
	FVector3d Position = FVector3d::ZeroVector; // inertial meters
	FVector3d Velocity = FVector3d::ZeroVector; // inertial m/s
	double Periapsis = 0.0;
	double Apoapsis = 0.0;
	double Period = 0.0;
	double TrueAnomaly = 0.0;
	double EccentricAnomaly = 0.0;
	bool bHyperbolic = false;
};

class GXCELESTIAL_API FGXKepler
{
public:
	/** Newton solve M = E - e sin E. */
	static double SolveEccentricAnomaly(double MeanAnomaly, double Eccentricity, int32 MaxIters = 12);

	static double MeanAnomalyAt(const FGXKeplerElements& E, double UniversalTime);

	static FGXOrbitalState Evaluate(const FGXKeplerElements& E, double UniversalTime);

	/** Convert inertial r,v to Kepler elements (ellipse; hyperbolic flagged). */
	static FGXKeplerElements FromState(const FVector3d& R, const FVector3d& V, double Mu, double UniversalTime);

	static double CircularVelocity(double Mu, double Radius);
	static double EscapeVelocity(double Mu, double Radius);
	static double Period(double Mu, double A);
	static double SphereOfInfluence(double SemiMajorAboutParent, double BodyMass, double ParentMass);

	/** Closed circular orbit over N periods should return near start (test helper). */
	static double ClosedOrbitError(const FGXKeplerElements& E, int32 Periods);
};
