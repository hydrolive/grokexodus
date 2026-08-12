// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelExodusCharacter.h"
#include "Voxel/VoxelTerrainToolComponent.h"
#include "Voxel/VoxelSphericalMovement.h"
#include "Voxel/VoxelPlanetActor.h"
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
	TerrainTool = CreateDefaultSubobject<UVoxelTerrainToolComponent>(TEXT("TerrainTool"));

	static ConstructorHelpers::FObjectFinder<UInputAction> IAMove(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IAJump(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IALook(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> IAMouseLook(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (IAMove.Succeeded()) MoveAction = IAMove.Object;
	if (IAJump.Succeeded()) JumpAction = IAJump.Object;
	if (IALook.Succeeded()) LookAction = IALook.Object;
	if (IAMouseLook.Succeeded()) MouseLookAction = IAMouseLook.Object;

	// Own capsule orientation for spherical gravity; do not use controller pitch/roll on the body
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AVoxelExodusCharacter::ConfigureFirstPersonCamera()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam)
	{
		return;
	}

	// Detach from head bone (template uses 0,90,-90 which inverts on a sphere).
	// Attach to capsule so camera is pure FPS eye height + relative pitch.
	Cam->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Cam->SetupAttachment(GetCapsuleComponent());
	Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Cam->SetRelativeRotation(FRotator::ZeroRotator);
	Cam->bUsePawnControlRotation = false;
	Cam->bEnableFirstPersonFieldOfView = false;
	Cam->bEnableFirstPersonScale = false;
	Cam->SetFieldOfView(90.0f);

	// Hide first-person arms if they fight camera orientation on the sphere
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

	if (UVoxelSphericalMovement* Move = GetSphericalMovement())
	{
		Move->bAutoFindPlanet = true;
		Move->TryFindPlanet();
		Move->SetWalkableFloorAngle(55.0f);
		Move->MaxStepHeight = 45.0f;
		Move->GroundFriction = 10.0f;
	}

	if (AController* C = GetController())
	{
		C->SetControlRotation(FRotator::ZeroRotator);
	}
}

void AVoxelExodusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGravityRelativeCamera();
}

UVoxelSphericalMovement* AVoxelExodusCharacter::GetSphericalMovement() const
{
	return Cast<UVoxelSphericalMovement>(GetCharacterMovement());
}

FVector AVoxelExodusCharacter::GetPlanetUp() const
{
	if (const UVoxelSphericalMovement* Move = GetSphericalMovement())
	{
		return Move->GetSphericalUpDir();
	}
	if (const UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		return -CMC->GetGravityDirection();
	}
	return GetActorUpVector();
}

void AVoxelExodusCharacter::UpdateGravityRelativeCamera()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam)
	{
		return;
	}

	// Capsule is already aligned to planet up by UVoxelSphericalMovement.
	// Camera only needs relative pitch: +pitch = look toward sky (actor +Z).
	const float Pitch = FMath::Clamp(LookPitch, -89.0f, 89.0f);
	Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Cam->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
}

void AVoxelExodusCharacter::DoAim(float Yaw, float Pitch)
{
	// Yaw: turn body around planet up (standard: +yaw = turn right)
	const FVector Up = GetPlanetUp().GetSafeNormal();
	if (!Up.IsNearlyZero() && !FMath::IsNearlyZero(Yaw))
	{
		const FQuat YawQ(Up, FMath::DegreesToRadians(Yaw));
		AddActorWorldRotation(YawQ, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Pitch: standard FPS — mouse up looks up (toward sky / +actor up)
	// Enhanced Input Mouse XY: +Y is typically mouse-up. If still inverted, toggle bInvertLookPitch.
	const float PitchDelta = bInvertLookPitch ? -Pitch : Pitch;
	LookPitch = FMath::Clamp(LookPitch + PitchDelta, -89.0f, 89.0f);

	// Keep control rotation in sync for any systems that read it (tools, etc.)
	if (AController* C = GetController())
	{
		FRotator R = GetActorRotation();
		R.Pitch = LookPitch;
		R.Roll = 0.0f;
		C->SetControlRotation(R);
	}
}

void AVoxelExodusCharacter::DoMove(float Right, float Forward)
{
	if (!GetController())
	{
		return;
	}

	const FVector Up = GetPlanetUp();

	// Move on the local horizon using camera facing (includes pitch projected)
	FVector ForwardDir = FVector::ZeroVector;
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		ForwardDir = FVector::VectorPlaneProject(Cam->GetForwardVector(), Up).GetSafeNormal();
	}
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
	}

	const FVector RightDir = FVector::CrossProduct(Up, ForwardDir).GetSafeNormal();
	AddMovementInput(ForwardDir, Forward);
	AddMovementInput(RightDir, Right);
}

void AVoxelExodusCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (DrillAction)
		{
			EIC->BindAction(DrillAction, ETriggerEvent::Started, this, &AVoxelExodusCharacter::OnDrillStarted);
			EIC->BindAction(DrillAction, ETriggerEvent::Completed, this, &AVoxelExodusCharacter::OnDrillCompleted);
			EIC->BindAction(DrillAction, ETriggerEvent::Canceled, this, &AVoxelExodusCharacter::OnDrillCompleted);
		}
		if (ToolModeAction)
		{
			EIC->BindAction(ToolModeAction, ETriggerEvent::Started, this, &AVoxelExodusCharacter::OnToolMode);
		}
		if (CycleMaterialAction)
		{
			EIC->BindAction(CycleMaterialAction, ETriggerEvent::Started, this, &AVoxelExodusCharacter::OnCycleMaterial);
		}
		if (SavePlanetAction)
		{
			EIC->BindAction(SavePlanetAction, ETriggerEvent::Started, this, &AVoxelExodusCharacter::OnSavePlanet);
		}
	}

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AVoxelExodusCharacter::OnDrillStarted);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AVoxelExodusCharacter::OnDrillCompleted);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AVoxelExodusCharacter::OnToolMode);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AVoxelExodusCharacter::OnCycleMaterial);
	PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AVoxelExodusCharacter::OnSavePlanet);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AVoxelExodusCharacter::OnToolMode);
}

void AVoxelExodusCharacter::OnDrillStarted()
{
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
