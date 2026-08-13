// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelWalkerPawn.h"
#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelPublicAPI.h"
#include "Voxel/VoxelCraftsmanshipComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

AVoxelWalkerPawn::AVoxelWalkerPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVoxelSphericalMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(60.f, 80.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("WalkerCam"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	Camera->bUsePawnControlRotation = false;

	if (UCharacterMovementComponent* M = GetCharacterMovement())
	{
		M->MaxWalkSpeed = DriveSpeed;
		M->MaxAcceleration = 3000.f;
		M->BrakingDecelerationWalking = 2500.f;
		M->bOrientRotationToMovement = false;
		M->bUseControllerDesiredRotation = false;
		M->JumpZVelocity = 500.f;
		M->AirControl = 0.3f;
	}
}

void AVoxelWalkerPawn::BeginPlay()
{
	Super::BeginPlay();
	TryFindPlanet();
	if (UVoxelSphericalMovement* M = GetSphericalMove())
	{
		M->bAutoFindPlanet = true;
		M->Planet = Planet;
		M->bAlignCapsuleToGravity = true;
		M->MaxWalkSpeed = DriveSpeed;
		M->TryFindPlanet();
	}
}

void AVoxelWalkerPawn::TryFindPlanet()
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

UVoxelSphericalMovement* AVoxelWalkerPawn::GetSphericalMove() const
{
	return Cast<UVoxelSphericalMovement>(GetCharacterMovement());
}

void AVoxelWalkerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Planet)
	{
		TryFindPlanet();
	}

	UpdateFootProbes();

	// Orientation: face horizon throttle direction, feet on planet
	FVector Up = FVector::UpVector;
	if (UVoxelSphericalMovement* M = GetSphericalMove())
	{
		Up = M->GetSphericalUpDir();
		M->MaxWalkSpeed = DriveSpeed;
	}
	else if (Planet)
	{
		Up = -Planet->GetGravityDirectionAt(GetActorLocation());
	}

	// Apply look yaw around up, pitch on camera only
	LookPitchAccum = FMath::Clamp(LookPitchAccum, -40.f, 40.f);
	FVector Horiz = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
	if (Horiz.IsNearlyZero())
	{
		Horiz = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
	}
	const FQuat YawQ(Up, FMath::DegreesToRadians(LookYawAccum * 0.f)); // yaw applied via TurnRight on actor
	(void)YawQ;

	if (Camera)
	{
		const FVector Right = FVector::CrossProduct(Up, Horiz).GetSafeNormal();
		const FQuat PitchQ(Right, FMath::DegreesToRadians(LookPitchAccum));
		const FVector Look = PitchQ.RotateVector(Horiz);
		const FQuat BodyQ = FRotationMatrix::MakeFromXZ(Horiz, Up).ToQuat();
		const FQuat WorldLook = FRotationMatrix::MakeFromXZ(Look, Up).ToQuat();
		Camera->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
		Camera->SetRelativeRotation((BodyQ.Inverse() * WorldLook).Rotator());
	}

	// Digital drive/steer from keys (reliable without Enhanced Input on walker)
	float Drive = 0.f, TurnIn = 0.f;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->IsInputKeyDown(EKeys::W)) Drive += 1.f;
		if (PC->IsInputKeyDown(EKeys::S)) Drive -= 1.f;
		if (PC->IsInputKeyDown(EKeys::D)) TurnIn += 1.f;
		if (PC->IsInputKeyDown(EKeys::A)) TurnIn -= 1.f;
	}
	Throttle = Drive;
	Steer = TurnIn;

	if (!FMath::IsNearlyZero(Throttle))
	{
		AddMovementInput(Horiz, bFeetOnTerrain ? Throttle : Throttle * 0.35f);
	}
	if (!FMath::IsNearlyZero(Steer))
	{
		const FQuat Turn(Up, FMath::DegreesToRadians(Steer * TurnRateDeg * DeltaSeconds));
		AddActorWorldRotation(Turn);
	}
}

void AVoxelWalkerPawn::UpdateFootProbes()
{
	bFeetOnTerrain = false;
	if (!Planet)
	{
		return;
	}
	const FVector Up = Planet ? -Planet->GetGravityDirectionAt(GetActorLocation()) : GetActorUpVector();
	const FVector Feet = GetActorLocation() - Up * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	bFeetOnTerrain = VoxelAPI::SphereHitsTerrain(Planet, Feet, FootProbeRadiusCm, 8);
}

void AVoxelWalkerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AVoxelWalkerPawn::LookYaw);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AVoxelWalkerPawn::LookPitch);
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AVoxelWalkerPawn::EjectDriver);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &AVoxelWalkerPawn::SelfDestruct);
}

void AVoxelWalkerPawn::MoveForward(float Value)
{
	Throttle = Value;
}

void AVoxelWalkerPawn::TurnRight(float Value)
{
	Steer = Value;
}

void AVoxelWalkerPawn::LookYaw(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	const FVector Up = GetSphericalMove() ? GetSphericalMove()->GetSphericalUpDir() : GetActorUpVector();
	AddActorWorldRotation(FQuat(Up, FMath::DegreesToRadians(Value * 2.5f)));
}

void AVoxelWalkerPawn::LookPitch(float Value)
{
	// Invert mouse Y for sky-up
	LookPitchAccum = FMath::Clamp(LookPitchAccum - Value * 2.5f, -40.f, 40.f);
}

float AVoxelWalkerPawn::GetCargoTotal() const
{
	float Sum = 0.f;
	for (const auto& P : CargoStock)
	{
		Sum += P.Value;
	}
	return Sum;
}

void AVoxelWalkerPawn::LoadCargoFrom(TMap<int32, float>& SourceStock)
{
	float Free = MaxCargoVolume - GetCargoTotal();
	if (Free <= 0.f)
	{
		return;
	}
	TArray<int32> Keys;
	SourceStock.GetKeys(Keys);
	for (int32 MatId : Keys)
	{
		float* Src = SourceStock.Find(MatId);
		if (!Src || *Src <= 0.f)
		{
			continue;
		}
		const float MoveAmt = FMath::Min(*Src, Free);
		CargoStock.FindOrAdd(MatId) += MoveAmt;
		*Src -= MoveAmt;
		Free -= MoveAmt;
		if (*Src <= KINDA_SMALL_NUMBER)
		{
			SourceStock.Remove(MatId);
		}
		if (Free <= 0.f)
		{
			break;
		}
	}
}

void AVoxelWalkerPawn::UnloadCargoTo(TMap<int32, float>& DestStock)
{
	for (auto& P : CargoStock)
	{
		if (P.Value > 0.f)
		{
			DestStock.FindOrAdd(P.Key) += P.Value;
		}
	}
	CargoStock.Reset();
}

FString AVoxelWalkerPawn::GetCargoStatusLine() const
{
	return FString::Printf(TEXT("Cargo %.1f / %.0f m3"), GetCargoTotal(), MaxCargoVolume);
}

void AVoxelWalkerPawn::EjectDriver()
{
	AController* C = GetController();
	if (!C)
	{
		return;
	}
	if (AGameModeBase* GM = UGameplayStatics::GetGameMode(this))
	{
		const FVector Up = GetSphericalMove() ? GetSphericalMove()->GetSphericalUpDir() : GetActorUpVector();
		const FVector ExitLoc = GetActorLocation() + Up * 120.f + GetActorRightVector() * 120.f;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (UClass* PawnClass = GM->DefaultPawnClass)
		{
			if (APawn* NewPawn = GetWorld()->SpawnActor<APawn>(PawnClass, ExitLoc, GetActorRotation(), SP))
			{
				// Return cargo to survivor craftsmanship stock
				if (UVoxelCraftsmanshipComponent* Craft = NewPawn->FindComponentByClass<UVoxelCraftsmanshipComponent>())
				{
					UnloadCargoTo(Craft->MaterialStock);
				}
				C->Possess(NewPawn);
				UE_LOG(LogVoxelWorld, Log, TEXT("Ejected from walker — cargo returned to survivor. Planet untouched."));
			}
		}
	}
}

void AVoxelWalkerPawn::SelfDestruct()
{
	// Fantasy pillar: lose walker AND its cargo; bunkers/planet stay
	const float Lost = GetCargoTotal();
	AController* C = GetController();
	if (C)
	{
		// Eject without unloading cargo — cargo dies with walker
		AGameModeBase* GM = UGameplayStatics::GetGameMode(this);
		if (GM && GM->DefaultPawnClass)
		{
			const FVector Up = GetSphericalMove() ? GetSphericalMove()->GetSphericalUpDir() : GetActorUpVector();
			const FVector ExitLoc = GetActorLocation() + Up * 150.f;
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (APawn* NewPawn = GetWorld()->SpawnActor<APawn>(GM->DefaultPawnClass, ExitLoc, GetActorRotation(), SP))
			{
				C->Possess(NewPawn);
			}
		}
	}
	UE_LOG(LogVoxelWorld, Log, TEXT("Walker destroyed. Cargo lost=%.2f. Bunkers and planet volume persist."), Lost);
	Destroy();
}
