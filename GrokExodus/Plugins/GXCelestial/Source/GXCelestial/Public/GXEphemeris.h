// Copyright Grok Exodus. All Rights Reserved.
// Dual-layer ephemeris: Kepler in doubles, UE scene stays body-fixed.
#pragma once

#include "CoreMinimal.h"
#include "GXBodyFrame.h"
#include "GXGravity.h"
#include "GXKepler.h"

/** Playable Sol / Earth / Moon. Earth is the active body (UE origin). */
struct GXCELESTIAL_API FGXEphemeris
{
	FGXBodyRotation EarthRot;
	FGXKeplerElements EarthHelio;
	FGXKeplerElements MoonEci;
	FGXAtmosphereModel Atmosphere;
	double PlanetRadius = 60000.0;
	double PlanetMu = 3.5316e10;
	double SolMu = 0.0;

	static FGXEphemeris PlayableEarth();

	FQuat4d InertialToBody(double UniversalTime) const;
	FVector3d BodyOmegaInertial(double UniversalTime) const;

	/** Unit vector, Earth-centered inertial, toward Sol. */
	FVector3d SunInertialDir(double UniversalTime) const;

	/** Unit vector in the body-fixed scene, toward Sol. */
	FVector3d SunBodyDir(double UniversalTime) const;

	FVector3d MoonInertialPos(double UniversalTime) const;
	FVector3d MoonBodyPos(double UniversalTime) const;

	/** Angular size (rad) of the playable Moon. */
	double MoonAngularRadius() const;
};
