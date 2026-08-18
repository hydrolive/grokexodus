// Copyright Grok Exodus. All Rights Reserved.
// Inertial vessel. ON_RAILS is Kepler; INTEGRATED is gravity + drag + heat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GXKepler.h"
#include "GXVessel.generated.h"

class UStaticMeshComponent;
class UGXSkySubsystem;

UENUM(BlueprintType)
enum class EGXVesselMode : uint8
{
	OnRails     UMETA(DisplayName = "On Rails"),
	Integrated  UMETA(DisplayName = "Integrated"),
};

UCLASS()
class GXCELESTIAL_API AGXVessel : public AActor
{
	GENERATED_BODY()

public:
	AGXVessel();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	EGXVesselMode Mode = EGXVesselMode::OnRails;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double MassKg = 2000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double DragAreaM2 = 4.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double Cd = 0.55;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double ParachuteCd = 12.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double BreakupHeat = 1.8e6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	double BreakupQ = 9.0e4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	bool bParachute = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX|Vessel")
	bool bThrusting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX|Vessel")
	bool bBroken = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX|Vessel")
	double LastHeat = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX|Vessel")
	double LastQ = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX|Vessel")
	double LastDensity = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GX|Vessel")
	double LastAltitude = 0.0;

	FGXKeplerElements Rails;
	FVector3d RInertial = FVector3d(62000, 0, 0);
	FVector3d VInertial = FVector3d(0, 754, 0);

	void SetCircularOrbit(double RadiusM, double InclinationRad, double Mu, double UniversalTime);
	void DeployParachute(bool bOn);
	void BreakApart(const TCHAR* Reason);
	FGXOrbitalState CurrentOrbit(double Mu) const;
	FString StatusLine() const;

	static AGXVessel* SpawnDemo(
		UWorld* World,
		EGXVesselMode InMode,
		double PlanetRadius,
		double Mu,
		double UniversalTime);

protected:
	UPROPERTY(VisibleAnywhere, Category = "GX|Vessel")
	TObjectPtr<UStaticMeshComponent> Hull;

	void StepIntegrated(double Dt, UGXSkySubsystem* Sky);
	void PoseFromInertial(UGXSkySubsystem* Sky);
};
