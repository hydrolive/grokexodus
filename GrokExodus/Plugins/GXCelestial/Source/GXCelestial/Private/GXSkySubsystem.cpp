// Copyright Grok Exodus. All Rights Reserved.

#include "GXSkySubsystem.h"
#include "GXCelestial.h"
#include "GXFrameSubsystem.h"
#include "GXPerf.h"
#include "GXVersion.h"
#include "GXVessel.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

const double UGXSkySubsystem::WarpSteps[UGXSkySubsystem::WarpCount] = {
	1.0, 2.0, 5.0, 10.0, 50.0, 100.0, 1000.0
};

void UGXSkySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Eph = FGXEphemeris::PlayableEarth();
	UniversalTime = 0.0;
	WarpIndex = 0;
	BindConsole();
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s sky subsystem init day=%.0fs year=%.0fs"),
		GX_VERSION_STRING, Eph.EarthRot.SiderealPeriod, 365.25 * Eph.EarthRot.SiderealPeriod);
}

void UGXSkySubsystem::Deinitialize()
{
	UnbindConsole();
	Super::Deinitialize();
}

void UGXSkySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureActors();
	SyncFrame();
	PoseSun();
	PoseMoon();
}

void UGXSkySubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const double T0 = FPlatformTime::Seconds();
	EnsureActors();

	double Warp = GetWarp();
	bWarpRefused = false;
	if (AGXVessel* Follow = FindFollowedVessel())
	{
		if (ShouldRefusePhysicsWarp(Follow->LastDensity, Follow->bThrusting) && Warp > 1.0)
		{
			bWarpRefused = true;
			Warp = 1.0;
		}
	}

	UniversalTime += static_cast<double>(DeltaTime) * Warp;
	LastSunBody = Eph.SunBodyDir(UniversalTime);
	const FVector3d MoonP = Eph.MoonBodyPos(UniversalTime);
	LastMoonBody = MoonP.GetSafeNormal();

	SyncFrame();
	PoseSun();
	PoseMoon();

	if (!bDemoSpawned)
	{
		SpawnDemoVessel();
		bDemoSpawned = true;
	}

	SkyMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
	static double LastLog = -1.0e9;
	if (UniversalTime - LastLog > 1.0)
	{
		LastLog = UniversalTime;
		GX_PERF(1, TEXT("GX-sky ut=%.1f warp=%.0f%s sun=(%.2f,%.2f,%.2f) moon=(%.2f,%.2f,%.2f) ms=%.2f"),
			UniversalTime, Warp, bWarpRefused ? TEXT(" refuse") : TEXT(""),
			LastSunBody.X, LastSunBody.Y, LastSunBody.Z,
			LastMoonBody.X, LastMoonBody.Y, LastMoonBody.Z, SkyMs);
	}
}

TStatId UGXSkySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGXSkySubsystem, STATGROUP_Tickables);
}

bool UGXSkySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UGXSkySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

double UGXSkySubsystem::GetWarp() const
{
	return WarpSteps[FMath::Clamp(WarpIndex, 0, WarpCount - 1)];
}

void UGXSkySubsystem::SetWarpIndex(int32 Index)
{
	WarpIndex = FMath::Clamp(Index, 0, WarpCount - 1);
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s warp x%.0f"), GX_VERSION_STRING, GetWarp());
}

void UGXSkySubsystem::StepWarp(int32 Delta)
{
	SetWarpIndex(WarpIndex + Delta);
}

void UGXSkySubsystem::SetUniversalTime(double Seconds)
{
	UniversalTime = Seconds;
}

bool UGXSkySubsystem::ShouldRefusePhysicsWarp(double DensityKgM3, bool bThrusting)
{
	return bThrusting || DensityKgM3 > 1.0e-4;
}

FString UGXSkySubsystem::FlightStrip() const
{
	const int32 Sec = FMath::Max(0, FMath::FloorToInt(UniversalTime));
	const int32 Hh = (Sec / 3600) % 100;
	const int32 Mm = (Sec / 60) % 60;
	const int32 Ss = Sec % 60;
	return FString::Printf(TEXT("UT %02d:%02d:%02d  W×%.0f%s  SUN %.0f°"),
		Hh, Mm, Ss, GetWarp(),
		bWarpRefused ? TEXT("!") : TEXT(""),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(LastSunBody.X, -1.0, 1.0))));
}

void UGXSkySubsystem::BindConsole()
{
	CmdWarp = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.warp"),
		TEXT("Time warp index 0..6 (1,2,5,10,50,100,1000)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!World)
				{
					return;
				}
				if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
				{
					if (Args.Num() > 0)
					{
						const int32 V = FCString::Atoi(*Args[0]);
						if (V >= 1 && V != 3 && V != 4)
						{
							for (int32 I = 0; I < WarpCount; ++I)
							{
								if (FMath::IsNearlyEqual(WarpSteps[I], static_cast<double>(V)))
								{
									Sky->SetWarpIndex(I);
									return;
								}
							}
						}
						Sky->SetWarpIndex(V);
					}
					else
					{
						UE_LOG(LogGXCelestial, Warning, TEXT("gx.warp = x%.0f"), Sky->GetWarp());
					}
				}
			}),
		ECVF_Default);

	CmdDump = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.sky.dump"),
		TEXT("Print ephemeris sun/moon/UT."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				if (UGXSkySubsystem* Sky = World ? World->GetSubsystem<UGXSkySubsystem>() : nullptr)
				{
					const FVector3d S = Sky->GetSunBodyDir();
					const FVector3d M = Sky->GetMoonBodyDir();
					UE_LOG(LogGXCelestial, Warning,
						TEXT("GX-sky dump ut=%.2f warp=%.0f sun=(%.3f,%.3f,%.3f) moon=(%.3f,%.3f,%.3f)"),
						Sky->GetUniversalTime(), Sky->GetWarp(), S.X, S.Y, S.Z, M.X, M.Y, M.Z);
				}
			}),
		ECVF_Default);

	CmdSpawn = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.vessel.spawn"),
		TEXT("Spawn a demo vessel: gx.vessel.spawn [rails|int]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!World)
				{
					return;
				}
				UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>();
				const EGXVesselMode Mode = (Args.Num() > 0 && Args[0].StartsWith(TEXT("int")))
					? EGXVesselMode::Integrated
					: EGXVesselMode::OnRails;
				const double R = Sky ? Sky->GetEphemeris().PlanetRadius : 60000.0;
				const double Mu = Sky ? Sky->GetEphemeris().PlanetMu : 3.5316e10;
				const double UT = Sky ? Sky->GetUniversalTime() : 0.0;
				AGXVessel::SpawnDemo(World, Mode, R, Mu, UT);
			}),
		ECVF_Default);
}

void UGXSkySubsystem::UnbindConsole()
{
	if (CmdWarp)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdWarp);
		CmdWarp = nullptr;
	}
	if (CmdDump)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdDump);
		CmdDump = nullptr;
	}
	if (CmdSpawn)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdSpawn);
		CmdSpawn = nullptr;
	}
}

void UGXSkySubsystem::EnsureActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!SunLight.IsValid())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			SunLight = *It;
			break;
		}
	}
	if (!MoonImpostor.IsValid())
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Moon = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
		if (Moon)
		{
			Moon->SetActorLabel(TEXT("GX_MoonImpostor"));
			if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
			{
				Moon->GetStaticMeshComponent()->SetStaticMesh(Sphere);
			}
			Moon->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Moon->GetStaticMeshComponent()->SetCastShadow(false);
			Moon->SetActorEnableCollision(false);
			MoonImpostor = Moon;
		}
	}
	bActorsReady = SunLight.IsValid();
}

void UGXSkySubsystem::PoseSun()
{
	ADirectionalLight* Light = SunLight.Get();
	if (!Light)
	{
		return;
	}
	const FVector Incoming(LastSunBody.X, LastSunBody.Y, LastSunBody.Z);
	if (Incoming.IsNearlyZero())
	{
		return;
	}
	Light->SetActorRotation((-Incoming.GetSafeNormal()).Rotation());
	if (UDirectionalLightComponent* C = Light->GetComponent())
	{
		C->SetMobility(EComponentMobility::Movable);
		C->SetAtmosphereSunLight(true);
		C->SetAtmosphereSunLightIndex(0);
		C->MarkRenderStateDirty();
	}
}

void UGXSkySubsystem::PoseMoon()
{
	AStaticMeshActor* Moon = MoonImpostor.Get();
	if (!Moon)
	{
		return;
	}
	const double VisualM = Eph.PlanetRadius + 28000.0;
	const FVector3d Dir = LastMoonBody.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}
	const double RVis = VisualM * Eph.MoonAngularRadius();
	const FVector Loc(
		static_cast<float>(Dir.X * VisualM * 100.0),
		static_cast<float>(Dir.Y * VisualM * 100.0),
		static_cast<float>(Dir.Z * VisualM * 100.0));
	Moon->SetActorLocation(Loc);
	const float S = static_cast<float>((RVis * 100.0) / 50.0);
	Moon->SetActorScale3D(FVector(S));
}

void UGXSkySubsystem::SyncFrame() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (UGXFrameSubsystem* Frame = World->GetSubsystem<UGXFrameSubsystem>())
	{
		Frame->SetActiveBody(TEXT("Earth"));
		Frame->SetUniversalTime(UniversalTime);
		Frame->SetInertialToBody(Eph.InertialToBody(UniversalTime));
		Frame->SetBodyOmegaInertial(Eph.BodyOmegaInertial(UniversalTime));
	}
}

void UGXSkySubsystem::SpawnDemoVessel()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AGXVessel> It(World); It; ++It)
	{
		DemoVessel = *It;
		return;
	}
	DemoVessel = AGXVessel::SpawnDemo(
		World, EGXVesselMode::OnRails, Eph.PlanetRadius, Eph.PlanetMu, UniversalTime);
}

AGXVessel* UGXSkySubsystem::FindFollowedVessel() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (AActor* Parent = Pawn->GetAttachParentActor())
		{
			if (AGXVessel* V = Cast<AGXVessel>(Parent))
			{
				return V;
			}
		}
	}
	return nullptr;
}
