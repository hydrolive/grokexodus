// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelExodusCharacter.h"
#include "Voxel/VoxelTerrainToolComponent.h"
#include "Voxel/VoxelCraftsmanshipComponent.h"
#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelWalkerPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AVoxelExodusCharacter::AVoxelExodusCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVoxelSphericalMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	// Tick AFTER movement so we apply look on the final capsule pose
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	TerrainTool = CreateDefaultSubobject<UVoxelTerrainToolComponent>(TEXT("TerrainTool"));
	Craftsmanship = CreateDefaultSubobject<UVoxelCraftsmanshipComponent>(TEXT("Craftsmanship"));

	static ConstructorHelpers::FObjectFinder<UInputAction> IAMove(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IAJump(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IALook(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IAMouseLook(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (IAMove.Succeeded()) MoveAction = IAMove.Object;
	if (IAJump.Succeeded()) JumpAction = IAJump.Object;
	if (IALook.Succeeded()) LookAction = IALook.Object;
	if (IAMouseLook.Succeeded()) MouseLookAction = IAMouseLook.Object;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		Cam->SetupAttachment(GetCapsuleComponent());
		Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
		Cam->SetRelativeRotation(FRotator::ZeroRotator);
		Cam->bUsePawnControlRotation = false;
		Cam->bEnableFirstPersonFieldOfView = false;
		Cam->bEnableFirstPersonScale = false;
		Cam->FieldOfView = 90.0f;
	}

	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(false);
		FPMesh->SetHiddenInGame(true);
		FPMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AVoxelExodusCharacter::ConfigureFirstPersonCamera()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam)
	{
		return;
	}

	if (Cam->GetAttachParent() != GetCapsuleComponent())
	{
		Cam->AttachToComponent(
			GetCapsuleComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Cam->SetRelativeRotation(FRotator::ZeroRotator);
	Cam->bUsePawnControlRotation = false;
	Cam->bEnableFirstPersonFieldOfView = false;
	Cam->bEnableFirstPersonScale = false;
	Cam->SetFieldOfView(90.0f);

	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(false);
		FPMesh->SetHiddenInGame(true);
	}
}

void AVoxelExodusCharacter::BeginPlay()
{
	Super::BeginPlay();

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	ConfigureFirstPersonCamera();
	LookPitch = 0.0f;
	bLookBasisValid = false;

	if (UVoxelSphericalMovement* Move = GetSphericalMovement())
	{
		Move->bAutoFindPlanet = true;
		Move->TryFindPlanet();
		Move->SetWalkableFloorAngle(60.0f);
		Move->MaxStepHeight = 50.0f;
		Move->GroundFriction = 8.0f;
		Move->BrakingDecelerationWalking = 2048.0f;
		Move->MaxWalkSpeed = 700.0f;
		Move->MaxAcceleration = 4096.0f;
		Move->AirControl = 0.45f;
		Move->GravityScale = 1.0f;
		// We own full orientation from look basis — do not let CMC spin the capsule
		Move->bAlignCapsuleToGravity = false;
		Move->bOrientRotationToMovement = false;
		Move->bUseControllerDesiredRotation = false;
	}

	EnsureLookBasis();
	ApplyLookAndBody();
	SyncToolModifiers();
}

void AVoxelExodusCharacter::SyncToolModifiers()
{
	if (TerrainTool && Craftsmanship)
	{
		TerrainTool->ToolModifiers = Craftsmanship->MakeModifiers();
	}
}

void AVoxelExodusCharacter::Tick(float DeltaSeconds)
{
	// Digital WASD fallback (does not fight Enhanced Input — only adds if keys held)
	if (const APlayerController* PC = Cast<APlayerController>(Controller))
	{
		float F = 0.f, R = 0.f;
		if (PC->IsInputKeyDown(EKeys::W)) F += 1.f;
		if (PC->IsInputKeyDown(EKeys::S)) F -= 1.f;
		if (PC->IsInputKeyDown(EKeys::D)) R += 1.f;
		if (PC->IsInputKeyDown(EKeys::A)) R -= 1.f;
		// Only use fallback when Enhanced Input produced nothing this frame
		if (!FMath::IsNearlyZero(F) || !FMath::IsNearlyZero(R))
		{
			if (DebugMoveInput.IsNearlyZero())
			{
				DoMove(R, F);
			}
		}
	}

	Super::Tick(DeltaSeconds);

	// Parallel-transport look onto current gravity plane, then apply pose
	EnsureLookBasis();
	ApplyLookAndBody();
	SyncToolModifiers();

	// Clear debug latches so next frame can detect fresh Enhanced Input
	DebugMoveInput = FVector2D::ZeroVector;
	DebugLookInput = FVector2D::ZeroVector;
}

UVoxelSphericalMovement* AVoxelExodusCharacter::GetSphericalMovement() const
{
	return Cast<UVoxelSphericalMovement>(GetCharacterMovement());
}

FVector AVoxelExodusCharacter::GetPlanetUp() const
{
	if (const UVoxelSphericalMovement* Move = GetSphericalMovement())
	{
		const FVector Up = Move->GetSphericalUpDir();
		if (!Up.IsNearlyZero())
		{
			return Up;
		}
	}
	if (const UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		const FVector Up = -CMC->GetGravityDirection();
		if (!Up.IsNearlyZero())
		{
			return Up.GetSafeNormal();
		}
	}
	const FVector Loc = GetActorLocation();
	if (!Loc.IsNearlyZero())
	{
		return Loc.GetSafeNormal(); // planet at origin fallback
	}
	return FVector::UpVector;
}

void AVoxelExodusCharacter::EnsureLookBasis()
{
	const FVector Up = GetPlanetUp().GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return;
	}

	if (!bLookBasisValid || LookHoriz.IsNearlyZero())
	{
		// Bootstrap: project actor forward (or world axes) onto horizon
		FVector Seed = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
		if (Seed.IsNearlyZero())
		{
			Seed = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
		}
		if (Seed.IsNearlyZero())
		{
			Seed = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
		}
		LookHoriz = Seed;
		bLookBasisValid = !LookHoriz.IsNearlyZero();
		return;
	}

	// Parallel transport: re-project previous horizon dir onto the new tangent plane.
	// This keeps facing stable while walking around the sphere (no false yaw from world axes).
	FVector Transported = FVector::VectorPlaneProject(LookHoriz, Up);
	if (Transported.SizeSquared() < 1e-6f)
	{
		// Looked straight along old up that became the new plane normal — pick a side vector
		Transported = FVector::VectorPlaneProject(GetActorRightVector(), Up);
		if (Transported.SizeSquared() < 1e-6f)
		{
			Transported = FVector::VectorPlaneProject(FVector::RightVector, Up);
		}
	}
	LookHoriz = Transported.GetSafeNormal();
	bLookBasisValid = true;
}

void AVoxelExodusCharacter::ApplyLookAndBody()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam || !bLookBasisValid)
	{
		return;
	}

	const FVector Up = GetPlanetUp().GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return;
	}

	// Keep horizon dir clean
	LookHoriz = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	if (LookHoriz.IsNearlyZero())
	{
		bLookBasisValid = false;
		EnsureLookBasis();
		if (LookHoriz.IsNearlyZero())
		{
			return;
		}
	}

	const FVector LookRight = FVector::CrossProduct(Up, LookHoriz).GetSafeNormal();
	if (LookRight.IsNearlyZero())
	{
		return;
	}

	const float PitchClamped = FMath::Clamp(LookPitch, -89.0f, 89.0f);
	const FQuat PitchQ(LookRight, FMath::DegreesToRadians(-PitchClamped));
	const FVector LookFwd = PitchQ.RotateVector(LookHoriz).GetSafeNormal();

	const FRotator BodyRot = FRotationMatrix::MakeFromXZ(LookHoriz, Up).Rotator();
	SetActorRotation(BodyRot);

	if (Cam->GetAttachParent() != GetCapsuleComponent())
	{
		Cam->AttachToComponent(
			GetCapsuleComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Cam->bEnableFirstPersonFieldOfView = false;
	Cam->bEnableFirstPersonScale = false;
	Cam->bUsePawnControlRotation = false;
	Cam->SetWorldRotation(FRotationMatrix::MakeFromXZ(LookFwd, Up).ToQuat().Rotator());
}

void AVoxelExodusCharacter::DoAim(float Yaw, float Pitch)
{
	DebugLookInput = FVector2D(Yaw, Pitch);

	const FVector Up = GetPlanetUp().GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return;
	}

	EnsureLookBasis();

	const float Scale = LookSensitivity;

	// Yaw: rotate horizon look around planet up (+yaw = turn right)
	if (!FMath::IsNearlyZero(Yaw))
	{
		const FQuat YawQ(Up, FMath::DegreesToRadians(Yaw * Scale));
		LookHoriz = YawQ.RotateVector(LookHoriz).GetSafeNormal();
		LookHoriz = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	}

	// Pitch: mouse-up → sky when bInvertLookPitch (default true for Enhanced Input Mouse Y)
	const float PitchDelta = (bInvertLookPitch ? -Pitch : Pitch) * Scale;
	LookPitch = FMath::Clamp(LookPitch + PitchDelta, -89.0f, 89.0f);

	bLookBasisValid = !LookHoriz.IsNearlyZero();
}

void AVoxelExodusCharacter::DoMove(float Right, float Forward)
{
	DebugMoveInput = FVector2D(Right, Forward);

	if (!Controller)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp && (MoveComp->MovementMode == MOVE_None || MoveComp->MovementMode == MOVE_Falling))
	{
		// Allow walking as soon as we have a floor; keep air control while falling
		if (MoveComp->MovementMode == MOVE_None)
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}

	const FVector Up = GetPlanetUp().GetSafeNormal();
	EnsureLookBasis();

	// Move on horizon using LOOK basis (not actor forward — avoids feedback with rotation)
	FVector ForwardDir = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		return;
	}
	const FVector RightDir = FVector::CrossProduct(Up, ForwardDir).GetSafeNormal();
	if (RightDir.IsNearlyZero())
	{
		return;
	}

	// Template passes (X=Right, Y=Forward) from IA_Move
	AddMovementInput(ForwardDir, Forward);
	AddMovementInput(RightDir, Right);
}

void AVoxelExodusCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AVoxelExodusCharacter::OnDrillStarted);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AVoxelExodusCharacter::OnDrillCompleted);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AVoxelExodusCharacter::OnToolMode);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AVoxelExodusCharacter::OnCycleMaterial);
	PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AVoxelExodusCharacter::OnSavePlanet);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AVoxelExodusCharacter::OnToolMode);
	// Phase 7
	PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &AVoxelExodusCharacter::OnClaimBunker);
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AVoxelExodusCharacter::OnSummonWalker);
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AVoxelExodusCharacter::OnCycleToolQuality);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AVoxelExodusCharacter::OnRepairTool);
}

void AVoxelExodusCharacter::OnDrillStarted()
{
	SyncToolModifiers();
	if (TerrainTool) TerrainTool->PrimaryFire(true);
}

void AVoxelExodusCharacter::OnDrillCompleted()
{
	if (TerrainTool) TerrainTool->PrimaryFire(false);
}

void AVoxelExodusCharacter::OnToolMode()
{
	if (TerrainTool) TerrainTool->CycleMode();
}

void AVoxelExodusCharacter::OnCycleMaterial()
{
	if (TerrainTool) TerrainTool->CyclePlaceMaterial(1);
}

void AVoxelExodusCharacter::OnSavePlanet()
{
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		It->SavePlanet();
		UE_LOG(LogVoxelWorld, Log, TEXT("Planet saved via character hotkey."));
		break;
	}
}

void AVoxelExodusCharacter::OnClaimBunker()
{
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		const int32 N = It->ClaimBunkerWorld(GetActorLocation(), BunkerHalfExtentsCm, true);
		UE_LOG(LogVoxelWorld, Log, TEXT("Bunker claimed: %d protected cells (permanent anchor)."), N);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
				FString::Printf(TEXT("Bunker claimed (%d cells). Protected from dig. Saved."), N));
		}
		break;
	}
}

void AVoxelExodusCharacter::OnSummonWalker()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !GetWorld())
	{
		return;
	}

	const FVector Up = GetPlanetUp();
	const FVector SpawnLoc = GetActorLocation() + LookHoriz * 300.f + Up * 50.f;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AVoxelWalkerPawn* Walker = GetWorld()->SpawnActor<AVoxelWalkerPawn>(
		AVoxelWalkerPawn::StaticClass(), SpawnLoc, GetActorRotation(), SP);
	if (Walker)
	{
		for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
		{
			Walker->Planet = *It;
			break;
		}
		// Phase 8: load material stock into walker cargo bay
		if (Craftsmanship)
		{
			Walker->LoadCargoFrom(Craftsmanship->MaterialStock);
		}
		// Keep survivor body in world? Possess transfers control; destroy old pawn
		APawn* Old = this;
		PC->Possess(Walker);
		// Unpossessed character — destroy so only walker remains
		Old->Destroy();
		UE_LOG(LogVoxelWorld, Log, TEXT("Possessed walker with cargo. F=eject (cargo back) X=destroy (cargo lost)."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
				FString::Printf(TEXT("Walker + cargo %.1f m3. F eject · X destroy (lose cargo)"), Walker->GetCargoTotal()));
		}
	}
}

void AVoxelExodusCharacter::OnCycleToolQuality()
{
	if (Craftsmanship)
	{
		Craftsmanship->CycleToolQuality(1);
		SyncToolModifiers();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, Craftsmanship->GetStatusLine());
		}
	}
}

void AVoxelExodusCharacter::OnRepairTool()
{
	if (Craftsmanship)
	{
		Craftsmanship->RepairTool(25.f);
		SyncToolModifiers();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, Craftsmanship->GetStatusLine());
		}
	}
}
