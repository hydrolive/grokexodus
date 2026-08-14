// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSunSetup.h"
#include "GXExodusCharacter.h"
#include "GXVoxelWorld.h"
#include "GXPlanetAtmosphere.h"
#include "GXHUDLayout.h"
#include "GXVersion.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AVoxelGameMode::AVoxelGameMode()
{
	// Lvl_VoxelPlanet overrides GameMode to this class — route it to the GX path.
	DefaultPawnClass = AGrokExodusSurvivor::StaticClass();
	PlayerControllerClass = AVoxelPlayerController::StaticClass();
	HUDClass = AGXHUDLayout::StaticClass();
}

void AVoxelGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
}

void AVoxelGameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Warning, TEXT("********** GX BUILD %s (%s) VoxelGameMode->GX **********"), GX_VERSION_STRING, GX_VERSION_DATE);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7, 30.f, FColor::Yellow,
			FString::Printf(TEXT("GX %s — if you do not see this, you are on old binaries"), GX_VERSION_STRING));
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->ClientSetHUD(AGXHUDLayout::StaticClass());
	}
	EnsureLighting();

	// Tear down the legacy planet so it cannot steal streaming / gravity / HUD.
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	AGXVoxelWorld* WorldActor = nullptr;
	for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
	{
		WorldActor = *It;
		break;
	}
	if (!WorldActor && GetWorld())
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		WorldActor = GetWorld()->SpawnActor<AGXVoxelWorld>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	}
	if (WorldActor)
	{
		WorldActor->ApplyEarthPlayDefaults();
		FGXPlanetAtmosphere::EnsureForPlanet(GetWorld(), WorldActor->PlanetRadius, 18000.0);
	}

	PlacePlayerOnSurface();
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AVoxelGameMode::PlacePlayerOnSurface, 0.25f, false);
}

void AVoxelGameMode::EnsureLighting()
{
	if (!bSpawnSunIfMissing || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AVoxelSunSetup> It(GetWorld()); It; ++It)
	{
		SunSetup = *It;
		SunSetup->EnsurePlanetLighting();
		return;
	}

	bool bHasSun = false;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		bHasSun = true;
		break;
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SunSetup = GetWorld()->SpawnActor<AVoxelSunSetup>(
		AVoxelSunSetup::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (SunSetup)
	{
		SunSetup->SetActorLabel(TEXT("VoxelSunSetup"));
		SunSetup->SunIntensity = 12.0f;
		SunSetup->SunTemperature = 5800.0f;
		SunSetup->SunPitchDegrees = -48.0f;
		SunSetup->SunYawDegrees = 35.0f;
		SunSetup->ShadowDistanceCm = 12000.0f;
		SunSetup->EnsurePlanetLighting();
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelGameMode: ensured sun lighting (hadDirectional=%s)"),
			bHasSun ? TEXT("true") : TEXT("false"));
	}
}

void AVoxelGameMode::EnsurePlanet()
{
	// Legacy no-op: AGXVoxelWorld is spawned in BeginPlay.
}

void AVoxelGameMode::PlacePlayerOnSurface()
{
	AGXVoxelWorld* WorldActor = nullptr;
	if (GetWorld())
	{
		for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
		{
			WorldActor = *It;
			break;
		}
	}
	if (!WorldActor)
	{
		return;
	}

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	WorldActor->PlacePawnOnSurface(Pawn, FVector(1.0f, 0.0f, 0.0f));
	if (AGrokExodusSurvivor* S = Cast<AGrokExodusSurvivor>(Pawn))
	{
		const FVector Up = -WorldActor->GetGravityDirectionAt(Pawn->GetActorLocation());
		FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
		}
		S->LookHoriz = Forward;
		S->LookPitch = 0.f;
	}
}
