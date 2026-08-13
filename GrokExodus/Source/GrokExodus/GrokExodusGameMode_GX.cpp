// Copyright Grok Exodus. All Rights Reserved.

#include "GrokExodusGameMode_GX.h"
#include "GXExodusCharacter.h"
#include "GXVoxelWorld.h"
#include "GXBodyMovement.h"
#include "GXHUDLayout.h"
#include "GXVersion.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSunSetup.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

AGXGameMode::AGXGameMode()
{
	DefaultPawnClass = AGrokExodusSurvivor::StaticClass();
	PlayerControllerClass = AVoxelPlayerController::StaticClass();
	HUDClass = AGXHUDLayout::StaticClass();
}

void AGXGameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Warning, TEXT("********** GX BUILD %s (%s) AGXGameMode **********"), GX_VERSION_STRING, GX_VERSION_DATE);
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
	EnsureWorld();
	PlacePlayerOnSurface();

	FTimerHandle H1, H2;
	GetWorldTimerManager().SetTimer(H1, this, &AGXGameMode::PlacePlayerOnSurface, 0.35f, false);
	GetWorldTimerManager().SetTimer(H2, this, &AGXGameMode::PlacePlayerOnSurface, 1.4f, false);
}

void AGXGameMode::EnsureLighting()
{
	if (!GetWorld())
	{
		return;
	}
	for (TActorIterator<AVoxelSunSetup> It(GetWorld()); It; ++It)
	{
		It->EnsurePlanetLighting();
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AVoxelSunSetup* Sun = GetWorld()->SpawnActor<AVoxelSunSetup>(FVector::ZeroVector, FRotator::ZeroRotator, SP))
	{
		Sun->EnsurePlanetLighting();
	}
}

void AGXGameMode::EnsureWorld()
{
	for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
	{
		VoxelWorld = *It;
		return;
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VoxelWorld = GetWorld()->SpawnActor<AGXVoxelWorld>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (VoxelWorld)
	{
		VoxelWorld->PlanetRadius = PlanetRadius;
		VoxelWorld->StreamRadius = FMath::Min(StreamRadius, 140.0f);
		VoxelWorld->UnloadRadius = VoxelWorld->StreamRadius + 50.0f;
		VoxelWorld->NearFieldRadius = 80.0f;
		VoxelWorld->MaxRelief = FMath::Clamp(PlanetRadius * 0.045f, 80.0f, 220.0f);
		VoxelWorld->bForceLOD0 = true;
		VoxelWorld->bAsyncMeshing = true;
		VoxelWorld->WarmupSeconds = 2.0f;
		VoxelWorld->WarmupMeshBuildsPerFrame = 24;
		VoxelWorld->MaxMeshBuildsPerFrame = 4;
		VoxelWorld->bAutoLoadOnBeginPlay = false;
	}
}

void AGXGameMode::PlacePlayerOnSurface()
{
	if (!VoxelWorld)
	{
		EnsureWorld();
	}
	if (!VoxelWorld)
	{
		return;
	}

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}

	const FVector Surface = VoxelWorld->FindSurfaceWorldLocation(FVector(1, 0, 0));
	const FVector Up = -VoxelWorld->GetGravityDirectionAt(Surface);
	VoxelWorld->UpdateStreaming(Surface + Up * 200.0f);
	VoxelWorld->FlushMeshQueue(256);

	const FVector SpawnLoc = Surface + Up * 180.0f;
	Pawn->SetActorLocation(SpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
	FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
	Pawn->SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UGXBodyMovement* Move = Cast<UGXBodyMovement>(Char->GetCharacterMovement()))
		{
			Move->TryFindField();
			Move->StopMovementImmediately();
			Move->SnapToSurface(true);
		}
	}
	if (AGrokExodusSurvivor* S = Cast<AGrokExodusSurvivor>(Pawn))
	{
		S->LookHoriz = Forward;
		S->LookPitch = 0.f;
	}
	VoxelWorld->UpdateStreaming(Pawn->GetActorLocation());
	VoxelWorld->FlushMeshQueue(64);
}
