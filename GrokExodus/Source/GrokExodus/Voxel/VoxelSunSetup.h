// Copyright Epic Games, Inc. All Rights Reserved.
// Ensures outdoor sun + atmosphere exist for voxel planet levels.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelSunSetup.generated.h"

/**
 * Place in a level or spawn from game mode.
 * Creates DirectionalLight (Sun), SkyAtmosphere, and SkyLight if missing.
 * SkyAtmosphere is configured as a sphere around the origin (see FGXPlanetAtmosphere).
 * ExponentialHeightFog is planar Z-up and is disabled on this planet.
 */
UCLASS(Blueprintable)
class AVoxelSunSetup : public AActor
{
	GENERATED_BODY()

public:
	AVoxelSunSetup();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Sun light intensity (UE lux-scale outdoor). ~10–15 is bright day with auto-exposure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunIntensity = 12.0f;

	/** Blackbody temperature (K). ~5800 = daylight sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunTemperature = 5800.0f;

	/** Pitch degrees. Ignored when bAimAtPlusXSpawn is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunPitchDegrees = 40.0f;

	/** Yaw degrees. Ignored when bAimAtPlusXSpawn is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunYawDegrees = 180.0f;

	/** Player spawns on the +X crust. Aim the sun there so lit PBR is not a black night side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	bool bAimAtPlusXSpawn = true;

	/** Soft sun angular diameter (degrees). Real Sun ≈ 0.53°. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float SunSourceAngle = 0.535f;

	/** Cascaded shadow distance in centimeters. Keep short for FPS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float ShadowDistanceCm = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	bool bSpawnAtmosphereIfMissing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	bool bSpawnSkyLightIfMissing = true;

	/** ExponentialHeightFog is planar Z-up and fights a sphere. Leave off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	bool bSpawnHeightFogIfMissing = false;

	/** Rebuild / ensure lighting actors. Safe to call multiple times. */
	UFUNCTION(BlueprintCallable, Category = "Sun")
	void EnsurePlanetLighting();

protected:
	UPROPERTY()
	TObjectPtr<class ADirectionalLight> SunLight;

	UPROPERTY()
	TObjectPtr<class ASkyAtmosphere> SkyAtmosphereActor;

	UPROPERTY()
	TObjectPtr<class ASkyLight> SkyLightActor;

	UPROPERTY()
	TObjectPtr<class AExponentialHeightFog> HeightFogActor;

	void ConfigureSun(ADirectionalLight* Light) const;
};
