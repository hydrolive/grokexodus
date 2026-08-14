// Copyright Grok Exodus. All Rights Reserved.

#include "GrokExodusGameMode_GX.h"
#include "GXExodusCharacter.h"
#include "GXVoxelWorld.h"
#include "GXBodyMovement.h"
#include "GXHUDLayout.h"
#include "GXVersion.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelSunSetup.h"
#include "GXPlanetAtmosphere.h"
#include "GXVoxelStamps.h"
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
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->ClientSetHUD(AGXHUDLayout::StaticClass());
	}
	EnsureLighting();
	EnsureWorld();
	if (GetWorld() && VoxelWorld)
	{
		FGXPlanetAtmosphere::EnsureForPlanet(GetWorld(), VoxelWorld->PlanetRadius, 18000.0);
	}
	PlacePlayerOnSurface();

	FTimerHandle H1;
	GetWorldTimerManager().SetTimer(H1, this, &AGXGameMode::PlacePlayerOnSurface, 0.25f, false);
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
		break;
	}
	if (!VoxelWorld)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		VoxelWorld = GetWorld()->SpawnActor<AGXVoxelWorld>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	}
	if (VoxelWorld)
	{
		VoxelWorld->ApplyEarthPlayDefaults();
		if (!FMath::IsNearlyEqual(PlanetRadius, 60000.0f) || !FMath::IsNearlyEqual(StreamRadius, 280.0f))
		{
			VoxelWorld->ConfigurePlanet(PlanetRadius, FGXPlanetStampParams::Earth().MaxRelief, StreamRadius);
		}
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

	VoxelWorld->PlacePawnOnSurface(Pawn, FVector(1, 0, 0));
	if (AGrokExodusSurvivor* S = Cast<AGrokExodusSurvivor>(Pawn))
	{
		const FVector Up = -VoxelWorld->GetGravityDirectionAt(Pawn->GetActorLocation());
		FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
		S->LookHoriz = Forward;
		S->LookPitch = 0.f;
	}
}
