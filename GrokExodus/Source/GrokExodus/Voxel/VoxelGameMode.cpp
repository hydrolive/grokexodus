// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSunSetup.h"
#include "GXExodusCharacter.h"
#include "GXVoxelWorld.h"
#include "GXHUDLayout.h"
#include "Engine/DirectionalLight.h"
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
	EnsureLighting();

	// Tear down the legacy planet so it cannot steal streaming / gravity / HUD.
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	bool bHaveGX = false;
	for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
	{
		bHaveGX = true;
		break;
	}
	if (!bHaveGX && GetWorld())
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AGXVoxelWorld* W = GetWorld()->SpawnActor<AGXVoxelWorld>(FVector::ZeroVector, FRotator::ZeroRotator, SP))
		{
			W->PlanetRadius = 4000.0f;
			W->StreamRadius = 140.0f;
			W->UnloadRadius = 190.0f;
			W->NearFieldRadius = 80.0f;
			W->MaxRelief = 180.0f;
			W->bForceLOD0 = true;
			W->bAsyncMeshing = true;
			W->WarmupSeconds = 2.0f;
			W->WarmupMeshBuildsPerFrame = 24;
			W->MaxMeshBuildsPerFrame = 4;
			W->bAutoLoadOnBeginPlay = false;
		}
	}

	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AVoxelGameMode::PlacePlayerOnSurface, 0.2f, false);
	FTimerHandle Handle2;
	GetWorldTimerManager().SetTimer(Handle2, this, &AVoxelGameMode::PlacePlayerOnSurface, 1.2f, false);
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

	const FVector Surface = WorldActor->FindSurfaceWorldLocation(FVector(1.0f, 0.0f, 0.0f));
	const FVector Up = -WorldActor->GetGravityDirectionAt(Surface);
	WorldActor->UpdateStreaming(Surface + Up * 200.0f);
	WorldActor->FlushMeshQueue(256);

	const FVector SpawnLoc = Surface + Up * 180.0f;
	Pawn->SetActorLocation(SpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);

	FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
	}
	Pawn->SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->SetGravityDirection(WorldActor->GetGravityDirectionAt(SpawnLoc));
			CMC->SetMovementMode(MOVE_Walking);
		}
	}
	if (AGrokExodusSurvivor* S = Cast<AGrokExodusSurvivor>(Pawn))
	{
		S->LookHoriz = Forward;
		S->LookPitch = 0.f;
	}

	WorldActor->UpdateStreaming(Pawn->GetActorLocation());
	WorldActor->FlushMeshQueue(96);
}
