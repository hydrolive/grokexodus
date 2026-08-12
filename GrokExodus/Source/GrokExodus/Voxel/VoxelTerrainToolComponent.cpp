// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelTerrainToolComponent.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelMaterialTable.h"
#include "GameFramework/Character.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

UVoxelTerrainToolComponent::UVoxelTerrainToolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	ToolModifiers.DigSpeedMul = 1.0f;
	ToolModifiers.RecoveryMul = 1.0f;
	ToolModifiers.PrecisionMul = 1.0f;
	ToolModifiers.WearMul = 1.0f;
}

void UVoxelTerrainToolComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoFindPlanet)
	{
		TryFindPlanet();
	}
}

void UVoxelTerrainToolComponent::TryFindPlanet()
{
	if (Planet || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
}

void UVoxelTerrainToolComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Planet && bAutoFindPlanet)
	{
		TryFindPlanet();
	}

	FireCooldown = FMath::Max(0.0f, FireCooldown - DeltaTime);
	UpdateRaycast();

	if (bPrimaryHeld)
	{
		ApplyTool(DeltaTime);
	}

	if (bDrawDebugPreview && bHasPreview && GetWorld())
	{
		const FColor Col = (Mode == EVoxelToolMode::Drill)
			? FColor(255, 80, 40)
			: FColor(80, 200, 255);
		DrawDebugSphere(GetWorld(), LastHit.Location, BrushRadius, 16, Col, false, -1.f, 0, 1.5f);
		DrawDebugLine(GetWorld(), GetTraceStart(), LastHit.Location, Col, false, -1.f, 0, 1.0f);
	}
}

FVector UVoxelTerrainToolComponent::GetTraceStart() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			FVector Loc;
			FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			return Loc;
		}
		return Pawn->GetActorLocation() + FVector(0, 0, 64);
	}
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FVector UVoxelTerrainToolComponent::GetTraceDir() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			FVector Loc;
			FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			return Rot.Vector();
		}
		return Pawn->GetActorForwardVector();
	}
	return FVector::ForwardVector;
}

bool UVoxelTerrainToolComponent::UpdateRaycast()
{
	bHasPreview = false;
	if (!Planet)
	{
		return false;
	}

	LastHit = Planet->RaycastVoxels(GetTraceStart(), GetTraceDir(), Reach);
	bHasPreview = LastHit.bHit;
	return LastHit.bHit;
}

void UVoxelTerrainToolComponent::ApplyTool(float DeltaTime)
{
	if (!Planet || !LastHit.bHit || FireCooldown > 0.0f)
	{
		return;
	}

	// Continuous tool: rate-limit slightly for remesh budget
	FireCooldown = 0.05f;

	// UE units are cm; volume uses meters (VoxelSize default 1m = 100 cm).
	// Convert brush to planet meters: RadiusMeters = BrushRadius / 100
	const float RadiusM = BrushRadius * 0.01f;
	const FVector Center = LastHit.Location;

	if (Mode == EVoxelToolMode::Drill)
	{
		const FVoxelDigResult Dig = Planet->DigSphere(Center, RadiusM, ToolModifiers, DigStrength);
		if (Dig.bSuccess)
		{
			UE_LOG(LogVoxelWorld, Verbose, TEXT("Drill: vol=%.2f yield=%.2f wear=%.2f mat=%d chunks=%d"),
				Dig.VolumeRemoved, Dig.YieldAmount, Dig.ToolWear, Dig.MaterialId, Dig.DirtyChunks.Num());
		}
	}
	else
	{
		// Place slightly along normal so we build outward (offset in UE cm)
		const FVector PlaceAt = Center + LastHit.Normal * (BrushRadius * 0.55f);
		const FVoxelDigResult Place = Planet->PlaceSphere(PlaceAt, RadiusM, PlaceMaterialId, ToolModifiers, PlaceStrength);
		if (Place.bSuccess)
		{
			UE_LOG(LogVoxelWorld, Verbose, TEXT("Place: mat=%d chunks=%d"), Place.MaterialId, Place.DirtyChunks.Num());
		}
	}
}

void UVoxelTerrainToolComponent::PrimaryFire(bool bPressed)
{
	bPrimaryHeld = bPressed;
	if (bPressed)
	{
		FireCooldown = 0.0f;
		UpdateRaycast();
		ApplyTool(0.016f);
	}
}

void UVoxelTerrainToolComponent::SecondaryFire()
{
	CycleMode();
}

void UVoxelTerrainToolComponent::CycleMode()
{
	Mode = (Mode == EVoxelToolMode::Drill) ? EVoxelToolMode::Place : EVoxelToolMode::Drill;
	UE_LOG(LogVoxelWorld, Log, TEXT("Tool mode: %s"), Mode == EVoxelToolMode::Drill ? TEXT("Drill") : TEXT("Place"));
}

void UVoxelTerrainToolComponent::CyclePlaceMaterial(int32 Direction)
{
	// Cycle 1..7 landscape materials
	PlaceMaterialId += Direction;
	if (PlaceMaterialId < 1) PlaceMaterialId = 7;
	if (PlaceMaterialId > 7) PlaceMaterialId = 1;
	UE_LOG(LogVoxelWorld, Log, TEXT("Place material id: %d"), PlaceMaterialId);
}
