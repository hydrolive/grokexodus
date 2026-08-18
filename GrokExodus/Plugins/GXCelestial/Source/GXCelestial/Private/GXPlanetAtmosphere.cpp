// Copyright Grok Exodus. All Rights Reserved.

#include "GXPlanetAtmosphere.h"
#include "GXCelestial.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"

void FGXPlanetAtmosphere::ConfigureSphericalSky(
	USkyAtmosphereComponent* Atmosphere,
	double PlanetRadiusMeters,
	double AtmosphereHeightMeters)
{
	if (!Atmosphere)
	{
		return;
	}

	const float RadiusKm = static_cast<float>(FMath::Max(PlanetRadiusMeters, 1000.0) / 1000.0);
	// Drag still uses 18 km. Visual height is Earth-like relative to 60 km
	// or the sky is mustard soup (18/60 vs Earth's 100/6371).
	const float HeightKm = 8.0f;

	// Planet center = component transform (origin). Horizon is radial, not Z-up.
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
	Atmosphere->SetWorldLocation(FVector::ZeroVector);
	Atmosphere->SetWorldRotation(FRotator::ZeroRotator);

	Atmosphere->SetBottomRadius(RadiusKm);
	Atmosphere->SetAtmosphereHeight(HeightKm);
	Atmosphere->SetGroundAlbedo(FColor(38, 52, 34));
	Atmosphere->SetMultiScatteringFactor(0.4f);

	Atmosphere->SetRayleighScatteringScale(1.15f);
	Atmosphere->SetRayleighScattering(FLinearColor(0.175287f, 0.409607f, 1.0f));
	Atmosphere->SetRayleighExponentialDistribution(6.0f);

	Atmosphere->SetMieScatteringScale(0.0012f);
	Atmosphere->SetMieScattering(FLinearColor(1.0f, 0.95f, 0.88f));
	Atmosphere->SetMieAbsorptionScale(0.0004f);
	Atmosphere->SetMieAbsorption(FLinearColor(0.90f, 0.90f, 0.90f));
	Atmosphere->SetMieAnisotropy(0.80f);
	Atmosphere->SetMieExponentialDistribution(1.2f);

	Atmosphere->SetAerialPespectiveViewDistanceScale(0.22f);
	Atmosphere->SetAerialPerspectiveStartDepth(0.02f);
	Atmosphere->SetHeightFogContribution(0.0f);
	(void)AtmosphereHeightMeters;

	Atmosphere->MarkRenderStateDirty();

	UE_LOG(LogGXCelestial, Warning,
		TEXT("GXPlanetAtmosphere: spherical R=%.2fkm H=%.2fkm rayleigh=6.0km mie=1.2km (visual Earth-blue)"),
		RadiusKm, HeightKm);
}

void FGXPlanetAtmosphere::ConfigureSphericalSky(
	ASkyAtmosphere* AtmosphereActor,
	double PlanetRadiusMeters,
	double AtmosphereHeightMeters)
{
	if (!AtmosphereActor)
	{
		return;
	}
	AtmosphereActor->SetActorLocation(FVector::ZeroVector);
	AtmosphereActor->SetActorRotation(FRotator::ZeroRotator);
	ConfigureSphericalSky(AtmosphereActor->GetComponent(), PlanetRadiusMeters, AtmosphereHeightMeters);
}

void FGXPlanetAtmosphere::DisablePlanarHeightFog(UExponentialHeightFogComponent* Fog)
{
	if (!Fog)
	{
		return;
	}
	Fog->SetFogDensity(0.0f);
	Fog->SetVolumetricFog(false);
	Fog->SetVisibility(false);
	Fog->SetHiddenInGame(true);
	Fog->MarkRenderStateDirty();
}

void FGXPlanetAtmosphere::DisablePlanarHeightFog(AExponentialHeightFog* FogActor)
{
	if (!FogActor)
	{
		return;
	}
	DisablePlanarHeightFog(FogActor->GetComponent());
	FogActor->SetActorHiddenInGame(true);
}

ASkyAtmosphere* FGXPlanetAtmosphere::EnsureForPlanet(UWorld* World, double PlanetRadiusMeters, double AtmosphereHeightMeters)
{
	if (!World)
	{
		return nullptr;
	}

	ASkyAtmosphere* Found = nullptr;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		Found = *It;
		break;
	}
	if (!Found)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Found = World->SpawnActor<ASkyAtmosphere>(
			ASkyAtmosphere::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
		if (Found)
		{
			Found->SetActorLabel(TEXT("SkyAtmosphere_Planet"));
		}
	}
	ConfigureSphericalSky(Found, PlanetRadiusMeters, AtmosphereHeightMeters);

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		DisablePlanarHeightFog(*It);
	}
	return Found;
}
