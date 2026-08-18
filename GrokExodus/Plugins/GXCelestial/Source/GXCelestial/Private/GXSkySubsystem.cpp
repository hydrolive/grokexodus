// Copyright Grok Exodus. All Rights Reserved.

#include "GXSkySubsystem.h"
#include "GXCelestial.h"
#include "GXFrameSubsystem.h"
#include "GXPerf.h"
#include "GXSunLambert.h"
#include "GXVersion.h"
#include "GXVessel.h"
#include "GXStarCatalog.h"
#include "GameFramework/PlayerController.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"

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
	UpdateStarField();

	SkyMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
	static double LastLog = -1.0e9;
	if (UniversalTime - LastLog > 10.0)
	{
		LastLog = UniversalTime;
		AGXVessel* FV = GetFollowedVessel();
		GX_PERF(1, TEXT("GX-sky ut=%.1f warp=%.0f%s %s dec=%.1f follow=%d valt=%.0f ms=%.2f"),
			UniversalTime, Warp, bWarpRefused ? TEXT(" refuse") : TEXT(""),
			*Eph.SeasonName(UniversalTime),
			FMath::RadiansToDegrees(Eph.SolarDeclination(UniversalTime)),
			FollowIndex, FV ? FV->LastAltitude : -1.0, SkyMs);
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
	if (Index < 0 || Index >= FGXStarCatalog::TotalCount)
	{
		return FVector3d::ZeroVector;
	}
	return Eph.InertialToBody(UniversalTime).RotateVector(FGXStarCatalog::Dir(Index));
}

int32 UGXSkySubsystem::StarCount() const
{
	return FGXStarCatalog::TotalCount;
}

bool UGXSkySubsystem::GetMoonSphere(FVector& OutLocCm, float& OutRadiusCm) const
{
	AStaticMeshActor* Moon = MoonImpostor.Get();
	if (!Moon)
	{
		return false;
	}
	OutLocCm = Moon->GetActorLocation();
	const FVector S = Moon->GetActorScale3D();
	OutRadiusCm = 50.0f * FMath::Max3(FMath::Abs(S.X), FMath::Abs(S.Y), FMath::Abs(S.Z));
	return OutRadiusCm > 1.0f;
}

float UGXSkySubsystem::StarMagnitude(int32 Index) const
{
	return FGXStarCatalog::Magnitude(Index);
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

void UGXSkySubsystem::AddFollowOrbit(float YawDeg, float PitchDeg)
{
	FollowYawDeg = FMath::UnwindDegrees(FollowYawDeg + YawDeg);
	FollowPitchDeg = FMath::Clamp(FollowPitchDeg + PitchDeg, -80.0f, 80.0f);
}

void UGXSkySubsystem::AddFollowZoom(float WheelSteps)
{
	const float F = FMath::Pow(0.85f, WheelSteps);
	FollowDistM = FMath::Clamp(FollowDistM * F, 18.0f, 12000.0f);
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
		const FVector Target = V->GetActorLocation();
		FVector Up = Target.GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector(1, 0, 0);
		}
		FVector East = FVector::CrossProduct(FVector(0, 0, 1), Up);
		if (East.IsNearlyZero())
		{
			East = FVector::CrossProduct(FVector(0, 1, 0), Up);
		}
		East.Normalize();
		const FVector North = FVector::CrossProduct(Up, East).GetSafeNormal();
		const FQuat YawQ(Up, FMath::DegreesToRadians(FollowYawDeg));
		const FVector Horiz = YawQ.RotateVector(North);
		const FVector Right = FVector::CrossProduct(Horiz, Up).GetSafeNormal();
		const FQuat PitchQ(Right, FMath::DegreesToRadians(FollowPitchDeg));
		const FVector Out = PitchQ.RotateVector(Horiz).GetSafeNormal();
		const FVector CamLoc = Target + Out * (FollowDistM * 100.0f) + Up * (FollowDistM * 8.0f);
		if (UCameraComponent* Cam = V->GetChaseCam())
		{
			Cam->SetUsingAbsoluteLocation(true);
			Cam->SetUsingAbsoluteRotation(true);
			Cam->SetWorldLocation(CamLoc);
			const FVector To = (Target - CamLoc).GetSafeNormal();
			if (!To.IsNearlyZero())
			{
				Cam->SetWorldRotation(FRotationMatrix::MakeFromXZ(To, Up).Rotator());
			}
		}
		if (PC->GetViewTarget() != V)
		{
			PC->SetViewTarget(V);
		}
		return;
	}
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (PC->GetViewTarget() != Pawn)
		{
			PC->SetViewTarget(Pawn);
		}
	}
}

void UGXSkySubsystem::EnsureStarField()
{
	if (StarISM.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Host = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (!Host)
	{
		return;
	}
	Host->SetActorLabel(TEXT("GX_StarField"));
	Host->SetActorLocation(FVector::ZeroVector);
	Host->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Host->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	Host->GetStaticMeshComponent()->SetVisibility(false);
	Host->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Host, TEXT("Stars"));
	ISM->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ISM->SetStaticMesh(Sphere);
	}
	UMaterialInterface* StarMat = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Voxel/Materials/M_GXStar.M_GXStar"));
	if (!StarMat)
	{
		StarMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	}
	if (StarMat)
	{
		StarMat->CheckMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		ISM->SetMaterial(0, StarMat);
	}
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(false);
	ISM->SetVisibleInRayTracing(false);
	ISM->bNeverDistanceCull = true;
	ISM->SetCullDistance(0.0f);
	ISM->SetReceivesDecals(false);
	ISM->bNeverDistanceCull = true;
	ISM->SetBoundsScale(256.0f);
	ISM->SetupAttachment(Host->GetRootComponent());
	ISM->RegisterComponent();
	const double StarRM = Eph.PlanetRadius + 80000.0;
	for (int32 I = 0; I < FGXStarCatalog::TotalCount; ++I)
	{
		const FVector3d D = FGXStarCatalog::Dir(I);
		const float Mag = FGXStarCatalog::Magnitude(I);
		const float Scale = FMath::Clamp(220.0f - Mag * 28.0f, 70.0f, 280.0f);
		const FVector Loc(
			static_cast<float>(D.X * StarRM * 100.0),
			static_cast<float>(D.Y * StarRM * 100.0),
			static_cast<float>(D.Z * StarRM * 100.0));
		ISM->AddInstance(FTransform(FRotator::ZeroRotator, Loc, FVector(Scale)));
	}
	ISM->UpdateBounds();
	ISM->MarkRenderStateDirty();
	StarHost = Host;
	StarISM = ISM;
	bStarsPlaced = true;
	UE_LOG(LogGXCelestial, Warning, TEXT("GX-%s stars: depth ISM n=%d r=%.0fkm mat=%s"),
		GX_VERSION_STRING, FGXStarCatalog::TotalCount, StarRM / 1000.0, *GetNameSafe(StarMat));
}

void UGXSkySubsystem::UpdateStarField()
{
	AActor* Host = StarHost.Get();
	UInstancedStaticMeshComponent* ISM = StarISM.Get();
	if (!Host || !ISM)
	{
		return;
	}
	const FQuat4d Q = Eph.InertialToBody(UniversalTime);
	Host->SetActorRotation(FQuat(Q.X, Q.Y, Q.Z, Q.W));
	FVector CamLoc = FVector::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				CamLoc = PC->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
	const FVector Up = CamLoc.GetSafeNormal();
	const float SunUp = static_cast<float>(
		LastSunBody.X * Up.X + LastSunBody.Y * Up.Y + LastSunBody.Z * Up.Z);
	ISM->SetVisibility(SunUp < -0.20f);
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
			Moon->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Moon->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			Moon->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Moon->GetStaticMeshComponent()->SetCastShadow(false);
			Moon->SetActorEnableCollision(false);
			MoonMID = FGXSunLambert::Apply(
				Moon->GetStaticMeshComponent(), FLinearColor(0.70f, 0.70f, 0.66f, 1.0f));
			MoonImpostor = Moon;
		}
	}
	EnsureStarField();
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
		C->SetUseTemperature(false);
		C->SetLightColor(FLinearColor(1.0f, 0.99f, 0.97f));
		C->SetIntensity(12.0f);
		C->SetAtmosphereSunLight(true);
		C->SetAtmosphereSunLightIndex(0);
		C->SetCastShadows(true);
		// Default CSM is ~200 m — past that the sun lights the far side
		// of every hill (0.13.8 "sun through the ground").
		C->DynamicShadowDistanceMovableLight = 800000.0f;
		C->DynamicShadowCascades = 4;
		C->CascadeDistributionExponent = 2.8f;
		C->MarkRenderStateDirty();
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			if (USkyLightComponent* SC = It->GetLightComponent())
			{
				SC->SetIntensity(0.28f);
				SC->bLowerHemisphereIsBlack = true;
			}
			break;
		}
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
	if (!MoonMID.IsValid())
	{
		MoonMID = FGXSunLambert::Apply(
			Moon->GetStaticMeshComponent(), FLinearColor(0.70f, 0.70f, 0.66f, 1.0f));
	}
	FGXSunLambert::SetSunDir(MoonMID.Get(), LastSunBody);
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
