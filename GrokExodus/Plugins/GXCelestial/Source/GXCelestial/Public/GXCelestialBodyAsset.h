// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GXKepler.h"
#include "GXGravity.h"
#include "GXBodyFrame.h"
#include "GXCelestialBodyAsset.generated.h"

UCLASS(BlueprintType)
class GXCELESTIAL_API UGXCelestialBodyAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FName BodyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FName ParentBodyId = NAME_None;

	/** Mean radius (meters). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	double RadiusMeters = 60000.0;

	/** Surface gravitational acceleration (m/s^2). μ = g R². */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	double SurfaceG = 9.81;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	double MassKg = 5.972e24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	double OrbitSemiMajorMeters = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	double OrbitEccentricity = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	double OrbitInclinationDeg = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation")
	double SiderealDaySeconds = 1440.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation")
	double ObliquityDeg = 23.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	bool bHasAtmosphere = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	double AtmosphereHeightMeters = 18000.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	double SeaLevelDensity = 1.225;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	double ScaleHeightMeters = 4200.0;

	UFUNCTION(BlueprintPure, Category = "Body")
	double GetMu() const { return SurfaceG * RadiusMeters * RadiusMeters; }

	FGXAtmosphereModel MakeAtmosphere() const;
	FGXBodyRotation MakeRotation() const;
	FGXKeplerElements MakeOrbitElements(double ParentMu) const;

	static UGXCelestialBodyAsset* MakeEarthDefaults();
	static void ApplyEarthDefaults(UGXCelestialBodyAsset* Asset);
	static void ApplyMoonDefaults(UGXCelestialBodyAsset* Asset);
};
