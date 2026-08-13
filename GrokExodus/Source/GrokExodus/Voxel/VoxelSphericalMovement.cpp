// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelPlanetActor.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

UVoxelSphericalMovement::UVoxelSphericalMovement()
{
	GravityScale = 1.0f;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	NavAgentProps.bCanCrouch = true;
	SetPlaneConstraintEnabled(false);

	BrakingDecelerationFalling = 500.0f;
	BrakingDecelerationWalking = 2048.0f;
	AirControl = 0.35f;
	FallingLateralFriction = 1.0f;
	GroundFriction = 8.0f;
	MaxWalkSpeed = 600.0f;
	MaxAcceleration = 2048.0f;
	JumpZVelocity = 420.0f;
	MaxStepHeight = 50.0f;
	SetWalkableFloorAngle(60.0f);
	// Stay stuck to voxel mesh
	PerchRadiusThreshold = 10.0f;
	bUseFlatBaseForFloorChecks = true;
	// Custom gravity: do not freeze when floor missing briefly during mesh cook
	bMaintainHorizontalGroundVelocity = true;
}

void UVoxelSphericalMovement::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoFindPlanet)
	{
		TryFindPlanet();
	}
	UpdateGravityDirection();
}

void UVoxelSphericalMovement::TryFindPlanet()
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

FVector UVoxelSphericalMovement::GetSphericalGravityDir() const
{
	if (Planet && UpdatedComponent)
	{
		const FVector Dir = Planet->GetGravityDirectionAt(UpdatedComponent->GetComponentLocation());
		if (!Dir.IsNearlyZero())
		{
			return Dir.GetSafeNormal();
		}
	}
	if (UpdatedComponent)
	{
		const FVector ToCenter = -UpdatedComponent->GetComponentLocation();
		if (!ToCenter.IsNearlyZero())
		{
			return ToCenter.GetSafeNormal();
		}
	}
	return FVector(0.f, 0.f, -1.f);
}

void UVoxelSphericalMovement::UpdateGravityDirection()
{
	const FVector Dir = GetSphericalGravityDir();
	SetGravityDirection(Dir);

	if (GravityStrength > 0.0f)
	{
		const float WorldG = FMath::Abs(GetWorld() ? GetWorld()->GetGravityZ() : -980.0f);
		if (WorldG > KINDA_SMALL_NUMBER)
		{
			GravityScale = GravityStrength / WorldG;
		}
	}
}

void UVoxelSphericalMovement::SnapToPlanetSurface(bool bZeroVelocity)
{
	if (!Planet || !UpdatedComponent)
	{
		TryFindPlanet();
	}
	if (!Planet || !UpdatedComponent)
	{
		return;
	}

	const FVector Center = Planet->GetPlanetCenter();
	FVector Pos = UpdatedComponent->GetComponentLocation();
	FVector Radial = Pos - Center;
	if (Radial.IsNearlyZero())
	{
		Radial = FVector(1, 0, 0);
	}
	const FVector Out = Radial.GetSafeNormal();

	// Density raycast from outside in
	const float OuterCm = (Planet->PlanetRadius + Planet->MaxRelief + 120.0f) * 100.0f;
	const FVector Start = Center + Out * OuterCm;
	const FVoxelHitResult Hit = Planet->RaycastVoxels(Start, -Out, OuterCm + 8000.0f);

	float CapsuleHalf = 96.0f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		CapsuleHalf = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	if (Hit.bHit)
	{
		// Place capsule fully above surface along radial up
		Pos = Hit.Location + Out * (CapsuleHalf + 30.0f);
	}
	else
	{
		// Fallback: planet radius + relief + capsule
		Pos = Center + Out * ((Planet->PlanetRadius + Planet->MaxRelief + 5.0f) * 100.0f + CapsuleHalf);
	}

	UpdatedComponent->SetWorldLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);

	UpdateGravityDirection();
	AlignCapsuleToGravity(1.0f);

	if (bZeroVelocity)
	{
		Velocity = FVector::ZeroVector;
		StopMovementImmediately();
	}

	// Force walking after surface is ready
	SetMovementMode(MOVE_Walking);
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
	if (!CurrentFloor.IsWalkableFloor())
	{
		// Nudge out and retry floor once
		UpdatedComponent->SetWorldLocation(Pos + Out * 50.0f, false, nullptr, ETeleportType::TeleportPhysics);
		FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
	}
}

void UVoxelSphericalMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!Planet && bAutoFindPlanet)
	{
		TryFindPlanet();
	}

	UpdateGravityDirection();

	if (bUnstickFromSolid)
	{
		UnstickIfBuried(DeltaTime);
	}

	if (bAlignCapsuleToGravity)
	{
		AlignCapsuleToGravity(DeltaTime);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If we're falling but density says ground is right below, try stick
	if (IsFalling() && Planet && UpdatedComponent)
	{
		const FVector Up = GetSphericalUpDir();
		const FVector Feet = UpdatedComponent->GetComponentLocation() - Up * 90.0f;
		const float D = Planet->SampleDensityWorld(Feet);
		if (D > 0.0f)
		{
			// Feet inside solid — snap up
			UnstickIfBuried(DeltaTime);
			SetMovementMode(MOVE_Walking);
		}
	}
}

void UVoxelSphericalMovement::AlignCapsuleToGravity(float DeltaSeconds)
{
	if (!UpdatedComponent)
	{
		return;
	}

	const FVector DesiredUp = GetSphericalUpDir();
	if (DesiredUp.IsNearlyZero())
	{
		return;
	}

	const FVector CurrentUp = UpdatedComponent->GetUpVector();
	// Already feet-down enough — leave yaw alone (character look owns facing)
	if (FVector::DotProduct(CurrentUp, DesiredUp) > 0.999f)
	{
		return;
	}

	// Preserve current facing on the new horizon plane (do not snap yaw to a world axis)
	FVector Forward = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), DesiredUp);
	if (Forward.SizeSquared() < 1e-4f)
	{
		Forward = FVector::VectorPlaneProject(UpdatedComponent->GetRightVector(), DesiredUp);
	}
	if (Forward.SizeSquared() < 1e-4f)
	{
		Forward = FVector::VectorPlaneProject(FVector::ForwardVector, DesiredUp);
	}
	if (Forward.SizeSquared() < 1e-4f)
	{
		Forward = FVector::VectorPlaneProject(FVector::UpVector, DesiredUp);
	}
	Forward.Normalize();

	const FQuat TargetQuat = FRotationMatrix::MakeFromXZ(Forward, DesiredUp).ToQuat();
	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();
	// Snap hard when very misaligned (spawn); otherwise gentle slerp
	const float Dot = FVector::DotProduct(CurrentUp, DesiredUp);
	const float Alpha = (Dot < 0.5f)
		? 1.0f
		: FMath::Clamp(DeltaSeconds * AlignSpeed, 0.0f, 1.0f);
	const FQuat Blended = FQuat::Slerp(CurrentQuat, TargetQuat, Alpha).GetNormalized();

	UpdatedComponent->SetWorldRotation(Blended, false, nullptr, ETeleportType::TeleportPhysics);
}

void UVoxelSphericalMovement::UnstickIfBuried(float DeltaSeconds)
{
	if (!Planet || !UpdatedComponent || !CharacterOwner)
	{
		return;
	}

	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const FVector Up = GetSphericalUpDir();
	float CapsuleHalf = 96.0f;
	if (CharacterOwner->GetCapsuleComponent())
	{
		CapsuleHalf = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	// Sample feet and body
	const float DFeet = Planet->SampleDensityWorld(Loc - Up * CapsuleHalf);
	const float DBody = Planet->SampleDensityWorld(Loc);
	if (DFeet <= 0.05f && DBody <= 0.05f)
	{
		return;
	}

	// Push out along radial until clear
	FVector Pos = Loc;
	for (int32 Step = 0; Step < 24; ++Step)
	{
		if (Planet->SampleDensityWorld(Pos) <= 0.0f
			&& Planet->SampleDensityWorld(Pos - Up * CapsuleHalf) <= 0.0f)
		{
			break;
		}
		Pos += Up * 15.0f; // 15 cm steps
	}
	UpdatedComponent->SetWorldLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);

	const float IntoGround = FVector::DotProduct(Velocity, -Up);
	if (IntoGround > 0.0f)
	{
		Velocity += Up * IntoGround;
	}
}
