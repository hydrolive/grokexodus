// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelExodusCharacter.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelSunSetup.h"
#include "Voxel/VoxelHUD.h"
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
	HUDClass = AVoxelHUD::StaticClass();
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

	// One place after a short delay so first crust meshes exist; one retry if still falling
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AVoxelGameMode::PlacePlayerOnSurface, 0.2f, false);

	FTimerHandle Handle2;
	GetWorldTimerManager().SetTimer(Handle2, this, &AVoxelGameMode::PlacePlayerOnSurface, 1.0f, false);
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
			Planet->PlanetRadius = PlanetRadius;
			// Working set: smaller stream + aggressive near field = solid underfoot fast
			Planet->StreamRadius = FMath::Min(StreamRadius, 160.0f);
			Planet->UnloadRadius = Planet->StreamRadius + 64.0f;
			Planet->NearFieldRadius = 80.0f;
			Planet->NearMeshBuildsPerFrame = 64;
			Planet->MaxRelief = FMath::Clamp(PlanetRadius * 0.045f, 80.0f, 220.0f);
			Planet->VoxelSize = 1.0f;
			Planet->Seed = 1337;
			Planet->bShowDistantSphere = true;
			Planet->bTerrainCastShadows = false;
			Planet->bForceLOD0 = true;
			Planet->bAsyncMeshing = false;
			Planet->MaxMeshBuildsPerFrame = 24;
			Planet->WarmupMeshBuildsPerFrame = 160;
			Planet->WarmupSeconds = 2.5f;
			Planet->LODBands = { { 400.0f, 0 } };
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

	// Surface along +X; stream and mesh a dense near ring BEFORE placing
	const FVector Surface = Planet->FindSurfaceWorldLocation(FVector(1.0f, 0.0f, 0.0f));
	const FVector Up = -Planet->GetGravityDirectionAt(Surface);
	const FVector StreamAt = Surface + Up * 200.0f;

	Planet->UpdateStreaming(StreamAt);
	// Drain near-field queue completely (underfoot), then some outer
	Planet->FlushMeshQueue(256);

	const float CapsuleHalf = 96.0f;
	const FVector SpawnLoc = Surface + Up * (CapsuleHalf + 80.0f);

	Pawn->SetActorLocation(SpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);

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
			Move->StopMovementImmediately();
			Move->SetGravityDirection(Planet->GetGravityDirectionAt(SpawnLoc));
			Move->SnapToPlanetSurface(true);
			Move->SetMovementMode(MOVE_Walking);
		}
		else if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->SetGravityDirection(Planet->GetGravityDirectionAt(Pawn->GetActorLocation()));
			CMC->SetMovementMode(MOVE_Walking);
		}
	}

	if (AVoxelExodusCharacter* VC = Cast<AVoxelExodusCharacter>(Pawn))
	{
		// Seed spherical look basis from spawn facing (parallel-transport model)
		VC->LookHoriz = Forward.GetSafeNormal();
		VC->LookPitch = 0.f;
	}

	Planet->UpdateStreaming(Pawn->GetActorLocation());
	Planet->FlushMeshQueue(64);

	// Re-snap after collision cook so we stand on mesh, not fall through empty air
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UVoxelSphericalMovement* Move = Cast<UVoxelSphericalMovement>(Char->GetCharacterMovement()))
		{
			Move->SnapToPlanetSurface(true);
			Move->SetMovementMode(MOVE_Walking);
		}
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("Player placed on surface at %s (up=%s)"),
		*Pawn->GetActorLocation().ToString(), *Up.ToString());
}
