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
	const float HeightKm = static_cast<float>(FMath::Clamp(AtmosphereHeightMeters / 1000.0, 4.0, 80.0));

	// Planet center = component transform (origin). Horizon is radial, not Z-up.
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
	Atmosphere->SetWorldLocation(FVector::ZeroVector);
	Atmosphere->SetWorldRotation(FRotator::ZeroRotator);

	Atmosphere->SetBottomRadius(RadiusKm);
	Atmosphere->SetAtmosphereHeight(HeightKm);
	Atmosphere->SetGroundAlbedo(FColor(92, 118, 72));
	Atmosphere->SetMultiScatteringFactor(1.0f);

	// Earth Rayleigh scale height is ~8 km. Shrink it so climbing a 2 km ridge
	// actually thins the sky instead of sitting inside a 100 km Earth LUT.
	const float RayleighKm = FMath::Clamp(HeightKm * 0.18f, 1.6f, 6.0f);
	Atmosphere->SetRayleighScatteringScale(1.0f);
	Atmosphere->SetRayleighScattering(FLinearColor(0.175287f, 0.409607f, 1.0f));
	Atmosphere->SetRayleighExponentialDistribution(RayleighKm);

	// Mie (aerosol / haze) lives in the first kilometre. Thick at the surface,
	// almost gone on a high ridge — that is the "atmosphere fog".
	const float MieKm = FMath::Clamp(HeightKm * 0.04f, 0.35f, 1.2f);
	Atmosphere->SetMieScatteringScale(0.0064f);
	Atmosphere->SetMieScattering(FLinearColor(1.0f, 0.92f, 0.82f));
	Atmosphere->SetMieAbsorptionScale(0.0012f);
	Atmosphere->SetMieAbsorption(FLinearColor(0.90f, 0.90f, 0.90f));
	Atmosphere->SetMieAnisotropy(0.78f);
	Atmosphere->SetMieExponentialDistribution(MieKm);

	Atmosphere->SetAerialPespectiveViewDistanceScale(0.55f);
	Atmosphere->SetAerialPerspectiveStartDepth(0.008f);
	Atmosphere->SetHeightFogContribution(0.0f);

	Atmosphere->MarkRenderStateDirty();

	UE_LOG(LogGXCelestial, Warning,
		TEXT("GXPlanetAtmosphere: spherical R=%.2fkm H=%.2fkm rayleigh=%.2fkm mie=%.2fkm (PlanetCenter)"),
		RadiusKm, HeightKm, RayleighKm, MieKm);
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
