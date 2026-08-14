// Copyright Grok Exodus. All Rights Reserved.
// Spherical SkyAtmosphere for a body-fixed planet at the world origin.
#pragma once

#include "CoreMinimal.h"

class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class ASkyAtmosphere;
class AExponentialHeightFog;
class UWorld;

/**
 * Unreal SkyAtmosphere defaults to PlanetTopAtAbsoluteWorldOrigin (Z-up ground
 * at the origin). Our player stands on the +X crust of a sphere centered at
 * the origin, so that default paints the limb 90° off the local horizon.
 *
 * Keep the SkyAtmosphere actor — it is the only engine path that gives a
 * spherical Rayleigh/Mie sky, aerial perspective, and height-dissipating haze.
 * Do not use ExponentialHeightFog for surface fog: it is planar Z-up.
 */
struct GXCELESTIAL_API FGXPlanetAtmosphere
{
	/** Planet radius and atmosphere thickness, both in meters. */
	static void ConfigureSphericalSky(
		USkyAtmosphereComponent* Atmosphere,
		double PlanetRadiusMeters,
		double AtmosphereHeightMeters = 18000.0);

	static void ConfigureSphericalSky(
		ASkyAtmosphere* AtmosphereActor,
		double PlanetRadiusMeters,
		double AtmosphereHeightMeters = 18000.0);

	/** Zero and hide planar Z-up height fog so it cannot fight the sphere. */
	static void DisablePlanarHeightFog(UExponentialHeightFogComponent* Fog);
	static void DisablePlanarHeightFog(AExponentialHeightFog* FogActor);

	/**
	 * Find-or-spawn SkyAtmosphere at the origin, configure it for this body,
	 * and disable any ExponentialHeightFog in the world.
	 */
	static ASkyAtmosphere* EnsureForPlanet(UWorld* World, double PlanetRadiusMeters, double AtmosphereHeightMeters = 18000.0);
};
