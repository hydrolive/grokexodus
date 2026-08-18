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
	// Short visual column so noon zenith is blue. Drag still uses 18 km.
	const float HeightKm = 4.5f;
	(void)AtmosphereHeightMeters;

	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
	Atmosphere->SetWorldLocation(FVector::ZeroVector);
	Atmosphere->SetWorldRotation(FRotator::ZeroRotator);

	Atmosphere->SetBottomRadius(RadiusKm);
	Atmosphere->SetAtmosphereHeight(HeightKm);
	Atmosphere->SetGroundAlbedo(FColor(22, 36, 20));
	Atmosphere->SetMultiScatteringFactor(0.0f);

	Atmosphere->SetRayleighScatteringScale(1.0f);
	Atmosphere->SetRayleighScattering(FLinearColor(0.175287f, 0.409607f, 1.0f));
	Atmosphere->SetRayleighExponentialDistribution(2.2f);

	Atmosphere->SetMieScatteringScale(0.00025f);
	Atmosphere->SetMieScattering(FLinearColor(1.0f, 0.96f, 0.90f));
	Atmosphere->SetMieAbsorptionScale(0.0002f);
	Atmosphere->SetMieAbsorption(FLinearColor(0.90f, 0.90f, 0.90f));
	Atmosphere->SetMieAnisotropy(0.76f);
	Atmosphere->SetMieExponentialDistribution(0.55f);

	Atmosphere->SetAerialPespectiveViewDistanceScale(0.06f);
	Atmosphere->SetAerialPerspectiveStartDepth(0.04f);
	Atmosphere->SetHeightFogContribution(0.0f);

	Atmosphere->MarkRenderStateDirty();

	UE_LOG(LogGXCelestial, Warning,
		TEXT("GXPlanetAtmosphere: spherical R=%.2fkm H=%.2fkm rayleigh=2.2km mie=0.55km (noon blue)"),
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
