// Copyright Grok Exodus. All Rights Reserved.

#include "GXBodyMovement.h"
#include "GXInterfaces.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/ActorInstanceHandle.h"
#include "EngineUtils.h"

UGXBodyMovement::UGXBodyMovement()
{
	GravityScale = 1.0f;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	BrakingDecelerationFalling = 500.0f;
	BrakingDecelerationWalking = 2048.0f;
	AirControl = 0.4f;
	GroundFriction = 8.0f;
	MaxWalkSpeed = 650.0f;
	MaxAcceleration = 2048.0f;
	JumpZVelocity = 700.0f;
	MaxStepHeight = 50.0f;
	SetWalkableFloorAngle(80.0f);
	bMaintainHorizontalGroundVelocity = false;
}

void UGXBodyMovement::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoFindField)
	{
		TryFindField();
	}
	UpdateGravity();
}

void UGXBodyMovement::TryFindField()
{
	if (FieldActor || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->ImplementsInterface(UGXGravityField::StaticClass()))
		{
			FieldActor = *It;
			break;
		}
	}
}

FVector UGXBodyMovement::GetGravityDir() const
{
	if (FieldActor)
	{
		if (const IGXGravityField* G = Cast<IGXGravityField>(FieldActor))
		{
			const FVector Acc = G->GetGravityCmS2(UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FieldActor->GetActorLocation());
			if (!Acc.IsNearlyZero())
			{
				return Acc.GetSafeNormal();
			}
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
	return FVector(0, 0, -1);
}

void UGXBodyMovement::UpdateGravity()
{
	const FVector Dir = GetGravityDir();
	SetGravityDirection(Dir);

	float Mag = 980.0f;
	if (FieldActor)
	{
		if (const IGXGravityField* G = Cast<IGXGravityField>(FieldActor))
		{
			const FVector Acc = G->GetGravityCmS2(UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector);
			Mag = Acc.Size();
		}
	}
	const float WorldG = FMath::Abs(GetWorld() ? GetWorld()->GetGravityZ() : -980.0f);
	if (WorldG > KINDA_SMALL_NUMBER)
	{
		GravityScale = Mag / WorldG;
	}
}

void UGXBodyMovement::NotifyPlayerJumped()
{
	JumpIgnoreSnapSeconds = 2.5f;
	AirborneSeconds = 0.0f;
}

void UGXBodyMovement::NotifyJustSpawned()
{
	SpawnSettleSeconds = 0.60f;
	AirborneSeconds = 0.0f;
	JumpIgnoreSnapSeconds = 0.0f;
	Velocity = FVector::ZeroVector;
	StopMovementImmediately();
	SetMovementMode(MOVE_Walking);
}

bool UGXBodyMovement::IsNearFloor(float MaxErrCm) const
{
	if (!UpdatedComponent)
	{
		return false;
	}
	FVector Surface, Desired;
	if (!FindLocalFloor(UpdatedComponent->GetComponentLocation(), Surface, Desired))
	{
		return false;
	}
	const float Err = FVector::DotProduct(Desired - UpdatedComponent->GetComponentLocation(), GetUpDir());
	return FMath::Abs(Err) <= FMath::Max(MaxErrCm, 1.0f);
}

bool UGXBodyMovement::IsJumpingUp() const
{
	return JumpIgnoreSnapSeconds > 0.0f && FVector::DotProduct(Velocity, GetUpDir()) > 40.0f;
}

bool UGXBodyMovement::FindStampSurface(const FVector& CapsuleLocation, FVector& OutSurfaceCm, FVector& OutCapsuleCm) const
{
	const IGXVoxelQuery* Q = Cast<IGXVoxelQuery>(FieldActor);
	if (!Q || !FieldActor)
	{
		return false;
	}

	const FVector Up = GetUpDir();
	if (Up.IsNearlyZero())
	{
		return false;
	}

	float Half = 88.0f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	constexpr float SkinCm = 2.0f;
	const FVector Origin = FieldActor->GetActorLocation();
	auto DensAt = [&](const FVector& WorldCm) -> float
	{
		const FVector L = WorldCm - Origin;
		return Q->SampleDensityMeters(FVector3d(L.X * 0.01, L.Y * 0.01, L.Z * 0.01));
	};

	// Solid > 0, air < 0. Search far enough that a fall into the planet still
	// finds the crust (0.7.26 only looked 12 m and gave up).
	const FVector Low = CapsuleLocation - Up * 8000.0f;
	FVector High = CapsuleLocation + Up * 8000.0f;
	const float DLow = DensAt(Low);
	const float DHere = DensAt(CapsuleLocation);
	if (DLow <= 0.0f && DHere <= 0.0f)
	{
		return false;
	}

	FVector Solid = (DLow > 0.0f) ? Low : CapsuleLocation;
	FVector Air = High;
	if (DensAt(Air) > 0.0f)
	{
		for (int32 Climb = 0; Climb < 40 && DensAt(Air) > 0.0f; ++Climb)
		{
			Air += Up * 800.0f;
		}
		if (DensAt(Air) > 0.0f)
		{
			return false;
		}
	}

	for (int32 I = 0; I < 18; ++I)
	{
		const FVector Mid = (Solid + Air) * 0.5f;
		if (DensAt(Mid) > 0.0f)
		{
			Solid = Mid;
		}
		else
		{
			Air = Mid;
		}
	}

	OutSurfaceCm = Air;
	OutCapsuleCm = OutSurfaceCm + Up * (Half + SkinCm);
	return true;
}

bool UGXBodyMovement::FindLocalFloor(const FVector& CapsuleLocation, FVector& OutSurfaceCm, FVector& OutCapsuleCm) const
{
	const IGXVoxelQuery* Q = Cast<IGXVoxelQuery>(FieldActor);
	if (!Q || !FieldActor)
	{
		return false;
	}

	const FVector Up = GetUpDir();
	if (Up.IsNearlyZero())
	{
		return false;
	}

	float Half = 88.0f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	constexpr float SkinCm = 2.0f;
	const FVector Origin = FieldActor->GetActorLocation();
	auto DensAt = [&](const FVector& WorldCm) -> float
	{
		const FVector L = WorldCm - Origin;
		return Q->SampleDensityMeters(FVector3d(L.X * 0.01, L.Y * 0.01, L.Z * 0.01));
	};

	const FVector Feet = CapsuleLocation - Up * Half;
	FVector Air = Feet;
	FVector Solid = FVector::ZeroVector;
	bool bFound = false;
	if (DensAt(Feet) > 0.0f)
	{
		// Soles already in solid — search a short way up for the interface.
		FVector Probe = Feet;
		for (int32 I = 0; I < 24; ++I)
		{
			Probe += Up * 8.0f;
			if (DensAt(Probe) <= 0.0f)
			{
				Air = Probe;
				Solid = Probe - Up * 8.0f;
				bFound = true;
				break;
			}
		}
	}
	else
	{
		for (float D = 8.0f; D <= 1200.0f; D += 16.0f)
		{
			const FVector Probe = Feet - Up * D;
			if (DensAt(Probe) > 0.0f)
			{
				Solid = Probe;
				Air = Probe + Up * 16.0f;
				bFound = true;
				break;
			}
		}
	}
	if (!bFound)
	{
		return false;
	}

	for (int32 I = 0; I < 16; ++I)
	{
		const FVector Mid = (Solid + Air) * 0.5f;
		if (DensAt(Mid) > 0.0f)
		{
			Solid = Mid;
		}
		else
		{
			Air = Mid;
		}
	}

	OutSurfaceCm = Air;
	OutCapsuleCm = OutSurfaceCm + Up * (Half + SkinCm);
	return true;
}

void UGXBodyMovement::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	Super::FindFloor(CapsuleLocation, OutFloorResult, bCanUseCachedLocation, DownwardSweepResult);
	if (OutFloorResult.IsWalkableFloor() || IsJumpingUp())
	{
		return;
	}

	FVector Surface, Desired;
	if (!FindLocalFloor(CapsuleLocation, Surface, Desired))
	{
		return;
	}

	const FVector Up = GetUpDir();
	float Half = 88.0f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	const float FloorDist = FVector::DotProduct(CapsuleLocation - Surface, Up) - Half;
	// Only a real floor when the soles are within a step of the local isosurface.
	if (FloorDist > MaxStepHeight + 12.0f || FloorDist < -Half)
	{
		return;
	}

	FHitResult Hit(1.0f);
	Hit.bBlockingHit = true;
	Hit.ImpactPoint = Surface;
	Hit.Location = Surface;
	Hit.Normal = Up;
	Hit.ImpactNormal = Up;
	Hit.TraceStart = CapsuleLocation;
	Hit.TraceEnd = Surface;
	Hit.Distance = FMath::Max(FloorDist, 0.0f);
	Hit.Time = 0.0f;
	if (FieldActor)
	{
		Hit.HitObjectHandle = FActorInstanceHandle(FieldActor.Get());
	}

	OutFloorResult.Clear();
	OutFloorResult.bBlockingHit = true;
	OutFloorResult.bWalkableFloor = true;
	OutFloorResult.bLineTrace = true;
	OutFloorResult.FloorDist = FloorDist;
	OutFloorResult.LineDist = FloorDist;
	OutFloorResult.HitResult = Hit;
}

void UGXBodyMovement::StickToStampFloor()
{
	if (!UpdatedComponent || IsJumpingUp())
	{
		return;
	}

	FVector Surface, Desired;
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	if (!FindLocalFloor(Loc, Surface, Desired))
	{
		return;
	}

	const FVector Up = GetUpDir();
	const float Err = FVector::DotProduct(Desired - Loc, Up);
	constexpr float DeadCm = 2.0f;
	// Only micro-correct a walk hover / shallow bury. 250 m stick yanked
	// every jump back to the crust at the apex (0.7.52).
	constexpr float StickDownCm = 10.0f;
	constexpr float StickUpCm = 80.0f;
	const bool bLift = Err > DeadCm && Err <= StickUpCm;
	const bool bSettle = Err < -DeadCm && Err >= -StickDownCm;
	if (bLift || bSettle)
	{
		UpdatedComponent->SetWorldLocation(Loc + Up * Err, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (CurrentFloor.IsWalkableFloor() || bLift || bSettle)
	{
		const float Into = FVector::DotProduct(Velocity, -Up);
		if (Into > 0.0f)
		{
			Velocity += Up * Into;
		}
		if (MovementMode == MOVE_Falling && (bLift || bSettle))
		{
			SetMovementMode(MOVE_Walking);
		}
	}
}

void UGXBodyMovement::SnapToSurface(bool bZeroVelocity)
{
	if (!FieldActor)
	{
		TryFindField();
	}
	if (!UpdatedComponent || !FieldActor)
	{
		return;
	}

	FVector Surface, Desired;
	if (FindStampSurface(UpdatedComponent->GetComponentLocation(), Surface, Desired))
	{
		UpdatedComponent->SetWorldLocation(Desired, false, nullptr, ETeleportType::TeleportPhysics);
	}

	UpdateGravity();
	AlignCapsule(1.0f);
	if (bZeroVelocity)
	{
		Velocity = FVector::ZeroVector;
		StopMovementImmediately();
	}
	else
	{
		const FVector Up = GetUpDir();
		const float Into = FVector::DotProduct(Velocity, -Up);
		if (Into > 0.0f)
		{
			Velocity += Up * Into;
		}
	}
	SetMovementMode(MOVE_Walking);
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
}

void UGXBodyMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!FieldActor && bAutoFindField)
	{
		TryFindField();
	}
	UpdateGravity();
	if (SpawnSettleSeconds > 0.0f)
	{
		SpawnSettleSeconds = FMath::Max(0.0f, SpawnSettleSeconds - DeltaTime);
	}
	if (bUnstickFromSolid && SpawnSettleSeconds <= 0.0f)
	{
		UnstickIfBuried(DeltaTime);
	}
	if (bAlignCapsuleToGravity)
	{
		AlignCapsule(DeltaTime);
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (JumpIgnoreSnapSeconds > 0.0f)
	{
		JumpIgnoreSnapSeconds -= DeltaTime;
	}

	// Banks have no collision. Stamp isosurface is the walk floor.
	// Old 0.08 s snap + "eject until feet are air" was the bounce.
	// Spawn settle: 0.25 s re-place + stick-lift was the first-spawn hop.
	if (SpawnSettleSeconds > 0.0f)
	{
		const FVector Up = GetUpDir();
		const float Into = FVector::DotProduct(Velocity, -Up);
		if (Into > 0.0f)
		{
			Velocity += Up * Into;
		}
		if (MovementMode == MOVE_Falling)
		{
			SetMovementMode(MOVE_Walking);
		}
	}
	else
	{
		StickToStampFloor();
	}

	// Under the walk tiles (shots 004847 / 004907). The underside is solid
	// within 12 m so the cave check would leave you inside the planet.
	if (!IsJumpingUp() && UpdatedComponent)
	{
		FVector StampSurf, StampCap;
		if (FindStampSurface(UpdatedComponent->GetComponentLocation(), StampSurf, StampCap))
		{
			const float BelowCm = FVector::DotProduct(
				StampSurf - UpdatedComponent->GetComponentLocation(), GetUpDir());
			if (BelowCm > 250.0f)
			{
				SnapToSurface(false);
				AirborneSeconds = 0.0f;
			}
		}
	}

	if (JumpIgnoreSnapSeconds > 0.0f)
	{
		AirborneSeconds = 0.0f;
	}
	else if (bSnapWhenAirborne && FieldActor && !CurrentFloor.IsWalkableFloor())
	{
		// A cave / quarry has solid under the feet. Do not yank back to the
		// outer crust — that was "I cannot dig a tunnel".
		if (HasSolidWithinMeters(12.0f))
		{
			AirborneSeconds = 0.0f;
		}
		else
		{
			AirborneSeconds += DeltaTime;
			if (AirborneSeconds >= AirborneSnapSeconds)
			{
				SnapToSurface(false);
				AirborneSeconds = 0.0f;
			}
		}
	}
	else
	{
		AirborneSeconds = 0.0f;
	}
}

void UGXBodyMovement::AlignCapsule(float DeltaSeconds)
{
	if (!UpdatedComponent)
	{
		return;
	}
	const FVector DesiredUp = GetUpDir();
	if (DesiredUp.IsNearlyZero())
	{
		return;
	}
	const FVector CurrentUp = UpdatedComponent->GetUpVector();
	if (FVector::DotProduct(CurrentUp, DesiredUp) > 0.999f)
	{
		return;
	}
	FVector Forward = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), DesiredUp);
	if (Forward.SizeSquared() < 1e-4f)
	{
		Forward = FVector::VectorPlaneProject(FVector::ForwardVector, DesiredUp);
	}
	Forward.Normalize();
	const FQuat Target = FRotationMatrix::MakeFromXZ(Forward, DesiredUp).ToQuat();
	const float Alpha = (FVector::DotProduct(CurrentUp, DesiredUp) < 0.5f)
		? 1.0f
		: FMath::Clamp(DeltaSeconds * AlignSpeed, 0.0f, 1.0f);
	UpdatedComponent->SetWorldRotation(
		FQuat::Slerp(UpdatedComponent->GetComponentQuat(), Target, Alpha).GetNormalized(),
		false, nullptr, ETeleportType::TeleportPhysics);
}

void UGXBodyMovement::UnstickIfBuried(float DeltaSeconds)
{
	if (!FieldActor || !UpdatedComponent)
	{
		return;
	}
	IGXVoxelQuery* Q = Cast<IGXVoxelQuery>(FieldActor);
	if (!Q)
	{
		return;
	}
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const FVector Up = GetUpDir();
	auto Dens = [&](const FVector& P)
	{
		const FVector3d M(P.X * 0.01, P.Y * 0.01, P.Z * 0.01);
		return Q->SampleDensityMeters(M);
	};
	// Feet in the floor is standing, not buried. Only eject if the torso is inside solid.
	if (Dens(Loc) <= 0.05f)
	{
		return;
	}
	FVector Pos = Loc;
	for (int32 Step = 0; Step < 80; ++Step)
	{
		if (Dens(Pos) <= 0.0f)
		{
			break;
		}
		Pos += Up * 20.0f;
	}
	UpdatedComponent->SetWorldLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	const float Into = FVector::DotProduct(Velocity, -Up);
	if (Into > 0.0f)
	{
		Velocity += Up * Into;
	}
	(void)DeltaSeconds;
}

bool UGXBodyMovement::HasSolidWithinMeters(float MaxMeters) const
{
	IGXVoxelQuery* Q = Cast<IGXVoxelQuery>(FieldActor);
	if (!Q || !UpdatedComponent)
	{
		return false;
	}
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const FVector Down = GetGravityDir();
	const float MaxCm = FMath::Max(MaxMeters, 0.5f) * 100.0f;
	for (float D = 25.0f; D <= MaxCm; D += 25.0f)
	{
		const FVector P = Loc + Down * D;
		const FVector3d M(P.X * 0.01, P.Y * 0.01, P.Z * 0.01);
		if (Q->SampleDensityMeters(M) > 0.0f)
		{
			return true;
		}
	}
	return false;
}
