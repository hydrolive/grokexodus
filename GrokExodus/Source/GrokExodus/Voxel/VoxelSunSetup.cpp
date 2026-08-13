// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelSunSetup.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h" // declares ASkyAtmosphere
#include "EngineUtils.h"
#include "GXVoxelWorld.h"

AVoxelSunSetup::AVoxelSunSetup()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = false;

	// Root so it shows in outliner
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVoxelSunSetup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	// Preview sun orientation in editor
	if (SunLight)
	{
		ConfigureSun(SunLight);
	}
#endif
}

void AVoxelSunSetup::BeginPlay()
{
	Super::BeginPlay();
	EnsurePlanetLighting();
}

void AVoxelSunSetup::ConfigureSun(ADirectionalLight* Light) const
{
	if (!Light)
	{
		return;
	}

	if (bAimAtPlusXSpawn)
	{
		// Incoming from above the +X spawn (up=+X). Travel is the opposite.
		const FVector Incoming = FVector(0.78f, 0.22f, 0.58f).GetSafeNormal();
		Light->SetActorRotation((-Incoming).Rotation());
	}
	else
	{
		Light->SetActorRotation(FRotator(SunPitchDegrees, SunYawDegrees, 0.0f));
	}

	if (UDirectionalLightComponent* C = Light->GetComponent())
	{
		C->SetMobility(EComponentMobility::Movable);
		C->SetIntensity(SunIntensity);
		C->SetUseTemperature(true);
		C->SetTemperature(SunTemperature);
		C->SetLightColor(FLinearColor(1.0f, 0.96f, 0.84f));
		C->SetAtmosphereSunLight(true);
		C->SetAtmosphereSunLightIndex(0);
		C->SetCastShadows(true);
		C->SetLightSourceAngle(SunSourceAngle);
		C->SetIndirectLightingIntensity(1.0f);
		C->SetVolumetricScatteringIntensity(1.0f);
		C->DynamicShadowDistanceMovableLight = ShadowDistanceCm;
		C->DynamicShadowCascades = 4;
		C->CascadeDistributionExponent = 2.5f;
		C->CascadeTransitionFraction = 0.12f;
		C->ShadowBias = 0.2f;
		C->ShadowSlopeBias = 0.5f;
		C->MarkRenderStateDirty();
	}
}

void AVoxelSunSetup::EnsurePlanetLighting()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Find existing sun / create
	if (!SunLight)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			SunLight = *It;
			break;
		}
	}
	if (!SunLight)
	{
		FActorSpawnParameters SP;
		SP.Name = TEXT("Sun_DirectionalLight");
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SunLight = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			FVector::ZeroVector,
			FRotator(SunPitchDegrees, SunYawDegrees, 0.0f),
			SP);
		if (SunLight)
		{
			SunLight->SetActorLabel(TEXT("Sun_DirectionalLight"));
		}
	}
	ConfigureSun(SunLight);

	if (bSpawnAtmosphereIfMissing && !SkyAtmosphereActor)
	{
		for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
		{
			SkyAtmosphereActor = *It;
			break;
		}
		if (!SkyAtmosphereActor)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SkyAtmosphereActor = World->SpawnActor<ASkyAtmosphere>(
				ASkyAtmosphere::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
			if (SkyAtmosphereActor)
			{
				SkyAtmosphereActor->SetActorLabel(TEXT("SkyAtmosphere_Planet"));
			}
		}
	}

	if (bSpawnSkyLightIfMissing && !SkyLightActor)
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			SkyLightActor = *It;
			break;
		}
		if (!SkyLightActor)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SkyLightActor = World->SpawnActor<ASkyLight>(
				ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
			if (SkyLightActor)
			{
				SkyLightActor->SetActorLabel(TEXT("SkyLight_Planet"));
			}
		}
	}
	if (SkyLightActor)
	{
		float RadiusCm = 400000.f;
		for (TActorIterator<AGXVoxelWorld> It(World); It; ++It)
		{
			RadiusCm = It->PlanetRadius * 100.f;
			break;
		}
		// Capture from the spawn crust, not the planet core (that sees dirt/black).
		SkyLightActor->SetActorLocation(FVector(RadiusCm + 300.f, 0.f, 0.f));
		if (USkyLightComponent* SC = SkyLightActor->GetLightComponent())
		{
			SC->SetMobility(EComponentMobility::Movable);
			SC->bRealTimeCapture = true;
			SC->SetIntensity(1.4f);
			SC->bLowerHemisphereIsBlack = false;
			SC->MarkRenderStateDirty();
		}
	}

	if (bSpawnHeightFogIfMissing && !HeightFogActor)
	{
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
		{
			HeightFogActor = *It;
			break;
		}
		if (!HeightFogActor)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			HeightFogActor = World->SpawnActor<AExponentialHeightFog>(
				AExponentialHeightFog::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
			if (HeightFogActor)
			{
				HeightFogActor->SetActorLabel(TEXT("HeightFog_Planet"));
				if (UExponentialHeightFogComponent* FC = HeightFogActor->GetComponent())
				{
					FC->SetFogDensity(0.008f);
					FC->SetFogHeightFalloff(0.12f);
					FC->SetFogInscatteringColor(FLinearColor(0.45f, 0.55f, 0.75f));
					FC->SetVolumetricFog(true);
					FC->VolumetricFogExtinctionScale = 0.6f;
					FC->MarkRenderStateDirty();
				}
			}
		}
	}

	const FVector Incoming = bAimAtPlusXSpawn
		? FVector(0.78f, 0.22f, 0.58f).GetSafeNormal()
		: -FRotator(SunPitchDegrees, SunYawDegrees, 0.f).Vector();
	UE_LOG(LogTemp, Warning, TEXT("VoxelSunSetup: intensity=%.1f +X NdotL=%.2f skylight=%s"),
		SunIntensity, Incoming.X, SkyLightActor ? *SkyLightActor->GetActorLocation().ToCompactString() : TEXT("none"));
}
