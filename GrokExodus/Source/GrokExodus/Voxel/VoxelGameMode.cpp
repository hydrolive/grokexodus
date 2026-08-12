// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelExodusCharacter.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelSunSetup.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AVoxelGameMode::AVoxelGameMode()
{
	DefaultPawnClass = AVoxelExodusCharacter::StaticClass();
	PlayerControllerClass = AVoxelPlayerController::StaticClass();
}

void AVoxelGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
}

void AVoxelGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureLighting();
	EnsurePlanet();

	// Stream + mesh first, then place. Repeat after collision cook settles.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AVoxelGameMode::PlacePlayerOnSurface, 0.35f, false);

	FTimerHandle Handle2;
	GetWorldTimerManager().SetTimer(Handle2, this, &AVoxelGameMode::PlacePlayerOnSurface, 1.5f, false);

	FTimerHandle Handle3;
	GetWorldTimerManager().SetTimer(Handle3, this, &AVoxelGameMode::PlacePlayerOnSurface, 3.0f, false);
}

void AVoxelGameMode::EnsureLighting()
{
	if (!bSpawnSunIfMissing || !GetWorld())
	{
		return;
	}

	// Prefer existing setup actor
	for (TActorIterator<AVoxelSunSetup> It(GetWorld()); It; ++It)
	{
		SunSetup = *It;
		SunSetup->EnsurePlanetLighting();
		return;
	}

	// Or existing directional light only — still spawn setup to wire atmosphere
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
		// Afternoon sun, outdoor intensity
		SunSetup->SunIntensity = 12.0f;
		SunSetup->SunTemperature = 5800.0f;
		SunSetup->SunPitchDegrees = -48.0f;
		SunSetup->SunYawDegrees = 35.0f;
		SunSetup->ShadowDistanceCm = 12000.0f; // shorter cascades = better FPS
		SunSetup->EnsurePlanetLighting();
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelGameMode: ensured sun lighting (hadDirectional=%s)"),
			bHasSun ? TEXT("true") : TEXT("false"));
	}
}

void AVoxelGameMode::EnsurePlanet()
{
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}

	if (!Planet && bSpawnPlanetIfMissing)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Planet = GetWorld()->SpawnActor<AVoxelPlanetActor>(
			AVoxelPlanetActor::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SP);
		if (Planet)
		{
			// Planetary scale: fine voxels + large radius. Cost controlled by stream + LOD.
			Planet->PlanetRadius = PlanetRadius;
			Planet->StreamRadius = StreamRadius;
			Planet->UnloadRadius = StreamRadius + 80.0f;
			Planet->MaxRelief = FMath::Clamp(PlanetRadius * 0.045f, 80.0f, 220.0f);
			Planet->VoxelSize = 1.0f; // fine dig resolution (do not coarsen for FPS)
			Planet->Seed = 1337;
			Planet->bShowDistantSphere = true;
			Planet->bTerrainCastShadows = false;
			// LOD0 in a wide near ring eliminates LOD crack-holes under the player
			Planet->bForceLOD0 = false;
			Planet->bAsyncMeshing = false; // sync mesh = no empty collision windows
			Planet->MaxMeshBuildsPerFrame = 12;
			Planet->WarmupMeshBuildsPerFrame = 96;
			Planet->WarmupSeconds = 8.0f;
			// Near-player full res (collision), mild LOD farther out
			Planet->LODBands = {
				{ 140.0f, 0 },
				{ 200.0f, 1 },
				{ 280.0f, 2 },
				{ 400.0f, 3 }
			};
			// Fresh procedural crust — avoid stale holey saves from earlier mesher bugs
			Planet->bAutoLoadOnBeginPlay = false;
		}
		UE_LOG(LogVoxelWorld, Log, TEXT("Spawned VoxelPlanet radius=%.0fm voxel=1m stream=%.0fm"), PlanetRadius, StreamRadius);
	}
}

void AVoxelGameMode::PlacePlayerOnSurface()
{
	if (!Planet)
	{
		EnsurePlanet();
	}
	if (!Planet)
	{
		return;
	}

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	// Build surface chunks (sync cook) before placing the pawn
	const FVector Surface = Planet->FindSurfaceWorldLocation(FVector(1.0f, 0.0f, 0.0f));
	const FVector Up = -Planet->GetGravityDirectionAt(Surface);

	Planet->UpdateStreaming(Surface + Up * 200.0f);
	Planet->FlushMeshQueue(64); // ensure collision meshes exist

	const float CapsuleHalf = 96.0f;
	const FVector SpawnLoc = Surface + Up * (CapsuleHalf + 50.0f);

	Pawn->SetActorLocation(SpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);

	// Feet down = toward planet center, forward along world +Y projected on horizon
	FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
	}
	const FRotator Rot = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
	Pawn->SetActorRotation(Rot);

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UVoxelSphericalMovement* Move = Cast<UVoxelSphericalMovement>(Char->GetCharacterMovement()))
		{
			Move->Planet = Planet;
			Move->SnapToPlanetSurface(true);
		}
		else if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->SetGravityDirection(Planet->GetGravityDirectionAt(Pawn->GetActorLocation()));
			CMC->SetMovementMode(MOVE_Walking);
		}
	}

	// Control rot: yaw/pitch relative to gravity (0,0 = look along horizon forward)
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetControlRotation(FRotator(0.f, 0.f, 0.f));
	}

	Planet->UpdateStreaming(Pawn->GetActorLocation());
	Planet->FlushMeshQueue(32);

	UE_LOG(LogVoxelWorld, Log, TEXT("Player placed on surface at %s (up=%s)"), *Pawn->GetActorLocation().ToString(), *Up.ToString());
}
