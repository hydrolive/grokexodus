// Copyright Grok Exodus. All Rights Reserved.

#include "GXSkySubsystem.h"
#include "GXCelestial.h"
#include "GXFrameSubsystem.h"
#include "GXPerf.h"
#include "GXVersion.h"
#include "GXVessel.h"
#include "GXStarCatalog.h"
#include "GameFramework/PlayerController.h"
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
	if (AGXVessel* Follow = GetFollowedVessel())
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

	ApplyFollowView();

	SkyMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
	static double LastLog = -1.0e9;
	if (UniversalTime - LastLog > 10.0)
	{
		LastLog = UniversalTime;
		GX_PERF(1, TEXT("GX-sky ut=%.1f warp=%.0f%s sun=(%.2f,%.2f,%.2f) %s dec=%.1f follow=%d ms=%.2f"),
			UniversalTime, Warp, bWarpRefused ? TEXT(" refuse") : TEXT(""),
			LastSunBody.X, LastSunBody.Y, LastSunBody.Z,
			*Eph.SeasonName(UniversalTime),
			FMath::RadiansToDegrees(Eph.SolarDeclination(UniversalTime)),
			FollowIndex, SkyMs);
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
	const double Dec = FMath::RadiansToDegrees(Eph.SolarDeclination(UniversalTime));
	return FString::Printf(TEXT("UT %02d:%02d:%02d  W×%.0f%s  %s dec%+.0f°"),
		Hh, Mm, Ss, GetWarp(),
		bWarpRefused ? TEXT("!") : TEXT(""),
		*Eph.SeasonName(UniversalTime), Dec);
}

FVector3d UGXSkySubsystem::StarBodyDir(int32 Index) const
{
	if (Index < 0 || Index >= FGXStarCatalog::Count)
	{
		return FVector3d::ZeroVector;
	}
	return Eph.InertialToBody(UniversalTime).RotateVector(FGXStarCatalog::Stars[Index].InertialDir());
}

AGXVessel* UGXSkySubsystem::GetFollowedVessel() const
{
	TArray<AGXVessel*> Vessels;
	CollectVessels(Vessels);
	if (Vessels.IsValidIndex(FollowIndex))
	{
		return Vessels[FollowIndex];
	}
	return nullptr;
}

void UGXSkySubsystem::CollectVessels(TArray<AGXVessel*>& Out) const
{
	Out.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AGXVessel> It(World); It; ++It)
	{
		if (*It && !It->bBroken)
		{
			Out.Add(*It);
		}
	}
}

void UGXSkySubsystem::CycleFollow()
{
	TArray<AGXVessel*> Vessels;
	CollectVessels(Vessels);
	if (Vessels.Num() == 0)
	{
		FollowIndex = -1;
		ApplyFollowView();
		return;
	}
	FollowIndex += 1;
	if (FollowIndex >= Vessels.Num())
	{
		FollowIndex = -1;
	}
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s follow %s"),
		GX_VERSION_STRING,
		FollowIndex < 0 ? TEXT("pawn") : *Vessels[FollowIndex]->GetName());
	ApplyFollowView();
}

void UGXSkySubsystem::ClearFollow()
{
	FollowIndex = -1;
	ApplyFollowView();
}

void UGXSkySubsystem::JumpToSeason(int32 SeasonIndex)
{
	UniversalTime = Eph.SeasonStartUT(SeasonIndex);
	LastSunBody = Eph.SunBodyDir(UniversalTime);
	SyncFrame();
	PoseSun();
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s season %s ut=%.0f dec=%.1f"),
		GX_VERSION_STRING, *Eph.SeasonName(UniversalTime), UniversalTime,
		FMath::RadiansToDegrees(Eph.SolarDeclination(UniversalTime)));
}

bool UGXSkySubsystem::ToggleParachuteOnFollowed()
{
	AGXVessel* V = GetFollowedVessel();
	if (!V)
	{
		TArray<AGXVessel*> Vessels;
		CollectVessels(Vessels);
		V = Vessels.Num() > 0 ? Vessels[0] : nullptr;
	}
	if (!V)
	{
		return false;
	}
	V->DeployParachute(!V->bParachute);
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s chute %s on %s"),
		GX_VERSION_STRING, V->bParachute ? TEXT("ON") : TEXT("off"), *V->GetName());
	return V->bParachute;
}

void UGXSkySubsystem::ApplyFollowView()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	if (AGXVessel* V = GetFollowedVessel())
	{
		V->UpdateChaseCamera();
		if (PC->GetViewTarget() != V)
		{
			PC->SetViewTargetWithBlend(V, 0.35f);
		}
		return;
	}
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (PC->GetViewTarget() != Pawn)
		{
			PC->SetViewTargetWithBlend(Pawn, 0.35f);
		}
	}
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

	CmdFollow = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.follow"),
		TEXT("Cycle camera: pawn → vessels."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				if (UGXSkySubsystem* Sky = World ? World->GetSubsystem<UGXSkySubsystem>() : nullptr)
				{
					Sky->CycleFollow();
				}
			}),
		ECVF_Default);

	CmdChute = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.vessel.chute"),
		TEXT("Toggle parachute on followed / first vessel (rails → integrated)."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				if (UGXSkySubsystem* Sky = World ? World->GetSubsystem<UGXSkySubsystem>() : nullptr)
				{
					Sky->ToggleParachuteOnFollowed();
				}
			}),
		ECVF_Default);

	CmdSeason = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("gx.sky.season"),
		TEXT("Jump UT: gx.sky.season 0..3 (spring summer autumn winter)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UGXSkySubsystem* Sky = World ? World->GetSubsystem<UGXSkySubsystem>() : nullptr)
				{
					Sky->JumpToSeason(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1);
				}
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
	if (CmdFollow)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdFollow);
		CmdFollow = nullptr;
	}
	if (CmdChute)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdChute);
		CmdChute = nullptr;
	}
	if (CmdSeason)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CmdSeason);
		CmdSeason = nullptr;
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
