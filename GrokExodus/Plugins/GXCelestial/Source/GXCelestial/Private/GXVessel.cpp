// Copyright Grok Exodus. All Rights Reserved.

#include "GXVessel.h"
#include "GXCelestial.h"
#include "GXEphemeris.h"
#include "GXFrameSubsystem.h"
#include "GXGravity.h"
#include "GXMath.h"
#include "GXPerf.h"
#include "GXSkySubsystem.h"
#include "GXVersion.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AGXVessel::AGXVessel()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	SetRootComponent(Hull);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Hull->SetStaticMesh(Sphere.Object);
	}
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hull->SetCastShadow(true);
	Hull->SetWorldScale3D(FVector(50.0f)); // 25 m visual so a pass is visible

	ChaseCam = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCam"));
	ChaseCam->SetupAttachment(Hull);
	ChaseCam->SetRelativeLocation(FVector(-12000.f, 0.f, 4000.f));
	ChaseCam->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	ChaseCam->bUsePawnControlRotation = false;
	ChaseCam->FieldOfView = 80.f;
}

void AGXVessel::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s vessel %s mode=%s"),
		GX_VERSION_STRING, *GetName(),
		Mode == EGXVesselMode::OnRails ? TEXT("rails") : TEXT("int"));
}

void AGXVessel::SetCircularOrbit(double RadiusM, double InclinationRad, double Mu, double UniversalTime)
{
	const FVector3d R(RadiusM, 0, 0);
	const double V = FGXKepler::CircularVelocity(Mu, RadiusM);
	const FVector3d Vel(0.0, V * FMath::Cos(InclinationRad), V * FMath::Sin(InclinationRad));
	Rails = FGXKepler::FromState(R, Vel, Mu, UniversalTime);
	RInertial = R;
	VInertial = Vel;
}

void AGXVessel::DeployParachute(bool bOn)
{
	bParachute = bOn;
	if (bOn && Mode == EGXVesselMode::OnRails)
	{
		Mode = EGXVesselMode::Integrated;
		UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s %s rails→int (chute)"),
			GX_VERSION_STRING, *GetName());
	}
}

void AGXVessel::UpdateChaseCamera()
{
	if (!ChaseCam)
	{
		return;
	}
	const FVector Loc = GetActorLocation();
	const FVector Rad = Loc.GetSafeNormal();
	UGXSkySubsystem* Sky = GetWorld() ? GetWorld()->GetSubsystem<UGXSkySubsystem>() : nullptr;
	FVector Back = FVector::ZeroVector;
	if (Sky)
	{
		if (UGXFrameSubsystem* Frame = GetWorld()->GetSubsystem<UGXFrameSubsystem>())
		{
			const FVector3d Vs = Frame->InertialVelocityToScene(RInertial, VInertial);
			Back = -FVector(Vs.X, Vs.Y, Vs.Z).GetSafeNormal();
		}
	}
	if (Back.IsNearlyZero())
	{
		Back = FVector::VectorPlaneProject(-FVector::UpVector, Rad).GetSafeNormal();
	}
	if (Back.IsNearlyZero())
	{
		Back = FVector::CrossProduct(Rad, FVector(0, 0, 1)).GetSafeNormal();
	}
	const FVector Cam = Loc + Back * 14000.f + Rad * 4500.f;
	ChaseCam->SetWorldLocation(Cam);
	const FVector ToShip = (Loc - Cam).GetSafeNormal();
	if (!ToShip.IsNearlyZero() && !Rad.IsNearlyZero())
	{
		ChaseCam->SetWorldRotation(FRotationMatrix::MakeFromXZ(ToShip, Rad).Rotator());
	}
}

void AGXVessel::BreakApart(const TCHAR* Reason)
{
	if (bBroken)
	{
		return;
	}
	bBroken = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s vessel %s breakup (%s) q=%.0f heat=%.0f"),
		GX_VERSION_STRING, *GetName(), Reason, LastQ, LastHeat);
	GX_PERF(1, TEXT("GX-vessel breakup %s q=%.0f heat=%.0f"), *GetName(), LastQ, LastHeat);
}

FGXOrbitalState AGXVessel::CurrentOrbit(double Mu) const
{
	return FGXKepler::Evaluate(FGXKepler::FromState(RInertial, VInertial, Mu, 0.0), 0.0);
}

FString AGXVessel::StatusLine() const
{
	return FString::Printf(TEXT("%s %s alt=%.0fm q=%.0f heat=%.0f%s%s"),
		*GetName(),
		Mode == EGXVesselMode::OnRails ? TEXT("rails") : TEXT("int"),
		LastAltitude, LastQ, LastHeat,
		bParachute ? TEXT(" chute") : TEXT(""),
		bBroken ? TEXT(" BROKEN") : TEXT(""));
}

void AGXVessel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBroken)
	{
		return;
	}
	UGXSkySubsystem* Sky = GetWorld() ? GetWorld()->GetSubsystem<UGXSkySubsystem>() : nullptr;
	if (!Sky)
	{
		return;
	}

	double Dt = static_cast<double>(DeltaSeconds);
	if (Mode == EGXVesselMode::OnRails)
	{
		Dt *= Sky->GetWarp();
		const FGXOrbitalState S = FGXKepler::Evaluate(Rails, Sky->GetUniversalTime());
		RInertial = S.Position;
		VInertial = S.Velocity;
	}
	else
	{
		const bool bRefuse = UGXSkySubsystem::ShouldRefusePhysicsWarp(LastDensity, bThrusting);
		if (!bRefuse)
		{
			Dt *= Sky->GetWarp();
		}
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt / 0.05), 1, 40);
		const double H = Dt / static_cast<double>(Steps);
		for (int32 I = 0; I < Steps; ++I)
		{
			StepIntegrated(H, Sky);
			if (bBroken)
			{
				return;
			}
		}
	}

	LastAltitude = RInertial.Size() - Sky->GetEphemeris().PlanetRadius;
	PoseFromInertial(Sky);
}

void AGXVessel::StepIntegrated(double Dt, UGXSkySubsystem* Sky)
{
	const FGXEphemeris& E = Sky->GetEphemeris();
	const double Mu = E.PlanetMu;
	const FVector3d Omega = E.BodyOmegaInertial(Sky->GetUniversalTime());
	auto Accel = [&](const FVector3d& R, const FVector3d& V) -> FVector3d
	{
		FVector3d A = FGXGravity::Acceleration(R, Mu);
		const double Alt = R.Size() - E.PlanetRadius;
		const double Rho = E.Atmosphere.DensityAt(Alt);
		const FVector3d VRel = V - FVector3d::CrossProduct(Omega, R);
		const double Spd = VRel.Size();
		const double UseCd = bParachute ? ParachuteCd : Cd;
		if (Rho > 0.0 && Spd > 1e-6 && MassKg > 1.0)
		{
			const double Drag = 0.5 * Rho * Spd * UseCd * DragAreaM2 / MassKg;
			A += VRel * (-Drag);
		}
		return A;
	};

	const FVector3d A0 = Accel(RInertial, VInertial);
	const FVector3d R1 = RInertial + VInertial * Dt;
	const FVector3d V1 = VInertial + A0 * Dt;
	const FVector3d A1 = Accel(R1, V1);
	RInertial = RInertial + (VInertial + V1) * (0.5 * Dt);
	VInertial = VInertial + (A0 + A1) * (0.5 * Dt);

	LastAltitude = RInertial.Size() - E.PlanetRadius;
	LastDensity = E.Atmosphere.DensityAt(LastAltitude);
	const FVector3d VRel = VInertial - FVector3d::CrossProduct(Omega, RInertial);
	LastQ = FGXGravity::DynamicPressure(LastDensity, VRel.Size());
	LastHeat = FGXGravity::HeatFlux(LastDensity, VRel.Size());

	if (LastAltitude < -50.0)
	{
		BreakApart(TEXT("lithobrake"));
		return;
	}
	if (!bParachute && (LastHeat > BreakupHeat || LastQ > BreakupQ))
	{
		BreakApart(TEXT("aero"));
	}
}

void AGXVessel::PoseFromInertial(UGXSkySubsystem* Sky)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (UGXFrameSubsystem* Frame = World->GetSubsystem<UGXFrameSubsystem>())
	{
		SetActorLocation(Frame->InertialToSceneCm(RInertial));
		return;
	}
	const FQuat4d R = Sky->GetEphemeris().InertialToBody(Sky->GetUniversalTime());
	const FVector3d Body = R.RotateVector(RInertial);
	SetActorLocation(GXUnits::MetersToCm3(Body));
}

AGXVessel* AGXVessel::SpawnDemo(
	UWorld* World,
	EGXVesselMode InMode,
	double PlanetRadius,
	double Mu,
	double UniversalTime)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGXVessel* V = World->SpawnActor<AGXVessel>(
		AGXVessel::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (!V)
	{
		return nullptr;
	}
	V->Mode = InMode;
	const double Alt = (InMode == EGXVesselMode::Integrated) ? 8000.0 : 2200.0;
	V->SetCircularOrbit(PlanetRadius + Alt, FMath::DegreesToRadians(28.0), Mu, UniversalTime);
	if (InMode == EGXVesselMode::Integrated)
	{
		V->VInertial *= 0.92; // decaying so drag/heat is exercised
	}
	V->SetActorLabel(InMode == EGXVesselMode::OnRails ? TEXT("GX_LEO_Rails") : TEXT("GX_LEO_Int"));
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s spawned %s a=%.0f"),
		GX_VERSION_STRING, *V->GetActorLabel(), V->Rails.SemiMajorAxis);
	return V;
}
