// Copyright Grok Exodus. All Rights Reserved.

#include "GXBodyMovement.h"
#include "GXInterfaces.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
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
	JumpZVelocity = 420.0f;
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

	if (IGXVoxelQuery* Q = Cast<IGXVoxelQuery>(FieldActor))
	{
		const FVector Loc = UpdatedComponent->GetComponentLocation();
		const FVector Up = GetUpDir();
		FVector Pos = Loc;
		for (int32 Step = 0; Step < 48; ++Step)
		{
			const FVector Sample = Pos - Up * 90.0f;
			const FVector3d LM(Sample.X * 0.01, Sample.Y * 0.01, Sample.Z * 0.01);
			if (Q->SampleDensityMeters(LM) <= 0.0f)
			{
				break;
			}
			Pos += Up * 20.0f;
		}
		// If we started in air, walk inward
		{
			const FVector Sample = Pos - Up * 90.0f;
			const FVector3d LM(Sample.X * 0.01, Sample.Y * 0.01, Sample.Z * 0.01);
			if (Q->SampleDensityMeters(LM) <= 0.0f)
			{
				for (int32 Step = 0; Step < 80; ++Step)
				{
					Pos -= Up * 15.0f;
					const FVector S2 = Pos - Up * 90.0f;
					const FVector3d L2(S2.X * 0.01, S2.Y * 0.01, S2.Z * 0.01);
					if (Q->SampleDensityMeters(L2) > 0.0f)
					{
						Pos += Up * 110.0f;
						break;
					}
				}
			}
		}
		UpdatedComponent->SetWorldLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	}

	UpdateGravity();
	AlignCapsule(1.0f);
	if (bZeroVelocity)
	{
		Velocity = FVector::ZeroVector;
		StopMovementImmediately();
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
	if (bUnstickFromSolid)
	{
		UnstickIfBuried(DeltaTime);
	}
	if (bAlignCapsuleToGravity)
	{
		AlignCapsule(DeltaTime);
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSnapWhenAirborne && FieldActor)
	{
		const bool bNoFloor = !CurrentFloor.IsWalkableFloor();
		const bool bInShallowHole = bNoFloor && HasSolidWithinMeters(4.0f);
		if (bNoFloor && !bInShallowHole)
		{
			AirborneSeconds += DeltaTime;
			if (AirborneSeconds >= AirborneSnapSeconds)
			{
				SnapToSurface(true);
				AirborneSeconds = 0.0f;
			}
		}
		else
		{
			AirborneSeconds = 0.0f;
		}
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
	float Half = 96.0f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
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
	for (int32 Step = 0; Step < 24; ++Step)
	{
		if (Dens(Pos) <= 0.0f && Dens(Pos - Up * Half) <= 0.0f)
		{
			break;
		}
		Pos += Up * 15.0f;
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
