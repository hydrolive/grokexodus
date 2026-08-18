// Copyright Grok Exodus. All Rights Reserved.

#include "GXExodusCharacter.h"
#include "GXTerrainToolComponent.h"
#include "GXVoxelInvokerComponent.h"
#include "GXBodyMovement.h"
#include "GXVoxelWorld.h"
#include "GXSkySubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "InputAction.h"

AGrokExodusSurvivor::AGrokExodusSurvivor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGXBodyMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	TerrainTool = CreateDefaultSubobject<UGXTerrainToolComponent>(TEXT("TerrainTool"));
	VoxelInvoker = CreateDefaultSubobject<UGXVoxelInvokerComponent>(TEXT("VoxelInvoker"));

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

void AGrokExodusSurvivor::ConfigureCamera()
{
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		if (Cam->GetAttachParent() != GetCapsuleComponent())
		{
			Cam->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
		Cam->SetRelativeRotation(FRotator::ZeroRotator);
		Cam->bUsePawnControlRotation = false;
		Cam->bEnableFirstPersonFieldOfView = false;
		Cam->bEnableFirstPersonScale = false;
		Cam->SetFieldOfView(90.0f);
	}
}

void AGrokExodusSurvivor::BeginPlay()
{
	Super::BeginPlay();
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	ConfigureCamera();
	JumpMaxHoldTime = 0.22f;
	if (UGXBodyMovement* Move = GetBodyMove())
	{
		Move->bAlignCapsuleToGravity = false;
		Move->bOrientRotationToMovement = false;
		Move->MaxWalkSpeed = 700.0f;
		Move->JumpZVelocity = 700.0f;
		Move->AirControl = 0.55f;
		Move->TryFindField();
	}
	EnsureLookBasis();
	ApplyLookAndBody();
}

void AGrokExodusSurvivor::Tick(float DeltaSeconds)
{
	if (const APlayerController* PC = Cast<APlayerController>(Controller))
	{
		float F = 0.f, R = 0.f;
		if (PC->IsInputKeyDown(EKeys::W)) F += 1.f;
		if (PC->IsInputKeyDown(EKeys::S)) F -= 1.f;
		if (PC->IsInputKeyDown(EKeys::D)) R += 1.f;
		if (PC->IsInputKeyDown(EKeys::A)) R -= 1.f;
		if (!FMath::IsNearlyZero(F) || !FMath::IsNearlyZero(R))
		{
			DoMove(R, F);
		}
	}
	Super::Tick(DeltaSeconds);
	EnsureLookBasis();
	ApplyLookAndBody();
}

UGXBodyMovement* AGrokExodusSurvivor::GetBodyMove() const
{
	return Cast<UGXBodyMovement>(GetCharacterMovement());
}

FVector AGrokExodusSurvivor::GetPlanetUp() const
{
	if (const UGXBodyMovement* Move = GetBodyMove())
	{
		const FVector Up = Move->GetUpDir();
		if (!Up.IsNearlyZero())
		{
			return Up;
		}
	}
	const FVector Loc = GetActorLocation();
	return Loc.IsNearlyZero() ? FVector::UpVector : Loc.GetSafeNormal();
}

void AGrokExodusSurvivor::EnsureLookBasis()
{
	const FVector Up = GetPlanetUp().GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return;
	}
	if (!bLookBasisValid || LookHoriz.IsNearlyZero())
	{
		FVector Seed = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
		if (Seed.IsNearlyZero()) Seed = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
		LookHoriz = Seed;
		bLookBasisValid = !LookHoriz.IsNearlyZero();
		return;
	}
	FVector Transported = FVector::VectorPlaneProject(LookHoriz, Up);
	if (Transported.SizeSquared() < 1e-6f)
	{
		Transported = FVector::VectorPlaneProject(FVector::RightVector, Up);
	}
	LookHoriz = Transported.GetSafeNormal();
	bLookBasisValid = true;
}

void AGrokExodusSurvivor::ApplyLookAndBody()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam || !bLookBasisValid) return;
	const FVector Up = GetPlanetUp().GetSafeNormal();
	LookHoriz = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	if (LookHoriz.IsNearlyZero()) return;
	if (Cam->GetAttachParent() != GetCapsuleComponent())
	{
		Cam->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Cam->bEnableFirstPersonFieldOfView = false;
		Cam->bEnableFirstPersonScale = false;
		Cam->bUsePawnControlRotation = false;
	}

	const FVector LookRight = FVector::CrossProduct(Up, LookHoriz).GetSafeNormal();
	if (LookRight.IsNearlyZero()) return;
	// Negative angle: +LookPitch pitches toward planet up (sky).
	const FQuat PitchQ(LookRight, FMath::DegreesToRadians(-FMath::Clamp(LookPitch, -89.f, 89.f)));
	const FVector LookFwd = PitchQ.RotateVector(LookHoriz).GetSafeNormal();
	const FRotator BodyRot = FRotationMatrix::MakeFromXZ(LookHoriz, Up).Rotator();
	SetActorRotation(BodyRot, ETeleportType::TeleportPhysics);
	Cam->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	// World rotation — never inherit the template head-socket (0, 90, -90).
	Cam->SetWorldRotation(FRotationMatrix::MakeFromXZ(LookFwd, Up).Rotator());
}

void AGrokExodusSurvivor::DoAim(float Yaw, float Pitch)
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			if (Sky->GetFollowIndex() >= 0)
			{
				const float P = (bInvertLookPitch ? -Pitch : Pitch) * LookSensitivity;
				Sky->AddFollowOrbit(Yaw * LookSensitivity, P);
				return;
			}
		}
	}
	const FVector Up = GetPlanetUp().GetSafeNormal();
	EnsureLookBasis();
	if (!FMath::IsNearlyZero(Yaw))
	{
		LookHoriz = FQuat(Up, FMath::DegreesToRadians(Yaw * LookSensitivity)).RotateVector(LookHoriz);
		LookHoriz = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	}
	LookPitch = FMath::Clamp(LookPitch + (bInvertLookPitch ? -Pitch : Pitch) * LookSensitivity, -89.f, 89.f);
}

void AGrokExodusSurvivor::DoMove(float Right, float Forward)
{
	if (!Controller) return;
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			if (Sky->GetFollowIndex() >= 0)
			{
				Sky->ClearFollow();
			}
		}
	}
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp && MoveComp->MovementMode == MOVE_None)
	{
		MoveComp->SetMovementMode(MOVE_Walking);
	}
	const FVector Up = GetPlanetUp().GetSafeNormal();
	EnsureLookBasis();
	const FVector ForwardDir = FVector::VectorPlaneProject(LookHoriz, Up).GetSafeNormal();
	const FVector RightDir = FVector::CrossProduct(Up, ForwardDir).GetSafeNormal();
	AddMovementInput(ForwardDir, Forward);
	AddMovementInput(RightDir, Right);
}

void AGrokExodusSurvivor::DoJumpStart()
{
	OnJumpPressed();
}

void AGrokExodusSurvivor::DoJumpEnd()
{
	OnJumpReleased();
}

void AGrokExodusSurvivor::OnJumpPressed()
{
	const uint64 Frame = GFrameCounter;
	if (LastJumpFrame == Frame)
	{
		return;
	}
	LastJumpFrame = Frame;

	UGXBodyMovement* Move = GetBodyMove();
	if (!Move)
	{
		Jump();
		return;
	}
	if (Move->MovementMode == MOVE_None)
	{
		Move->SetMovementMode(MOVE_Walking);
	}
	Move->NotifyPlayerJumped();
	Jump();
	if (!Move->IsFalling())
	{
		const FVector Up = GetPlanetUp();
		Move->Velocity += Up * Move->JumpZVelocity;
		Move->SetMovementMode(MOVE_Falling);
	}
}

void AGrokExodusSurvivor::OnJumpReleased()
{
	StopJumping();
}

void AGrokExodusSurvivor::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGrokExodusSurvivor::OnDrillStarted);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AGrokExodusSurvivor::OnDrillCompleted);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AGrokExodusSurvivor::OnToolMode);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AGrokExodusSurvivor::OnCycleMaterial);
	PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AGrokExodusSurvivor::OnSaveWorld);
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AGrokExodusSurvivor::OnCycleQuality);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AGrokExodusSurvivor::OnToolMode);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AGrokExodusSurvivor::OnJumpPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AGrokExodusSurvivor::OnJumpReleased);
	PlayerInputComponent->BindKey(EKeys::Period, IE_Pressed, this, &AGrokExodusSurvivor::OnWarpUp);
	PlayerInputComponent->BindKey(EKeys::Comma, IE_Pressed, this, &AGrokExodusSurvivor::OnWarpDown);
	PlayerInputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &AGrokExodusSurvivor::OnWarpUp);
	PlayerInputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &AGrokExodusSurvivor::OnWarpDown);
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AGrokExodusSurvivor::OnFollowVessel);
	PlayerInputComponent->BindKey(EKeys::P, IE_Pressed, this, &AGrokExodusSurvivor::OnToggleChute);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AGrokExodusSurvivor::OnFollowZoomIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AGrokExodusSurvivor::OnFollowZoomOut);
}

void AGrokExodusSurvivor::OnWarpUp()
{
	StepWarp(1);
}

void AGrokExodusSurvivor::OnWarpDown()
{
	StepWarp(-1);
}

void AGrokExodusSurvivor::StepWarp(int32 Delta)
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			Sky->StepWarp(Delta);
		}
	}
}

void AGrokExodusSurvivor::OnFollowVessel()
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			Sky->CycleFollow();
		}
	}
}

void AGrokExodusSurvivor::OnToggleChute()
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			Sky->ToggleParachuteOnFollowed();
		}
	}
}

void AGrokExodusSurvivor::OnFollowZoomIn()
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			if (Sky->GetFollowIndex() >= 0)
			{
				Sky->AddFollowZoom(1.0f);
			}
		}
	}
}

void AGrokExodusSurvivor::OnFollowZoomOut()
{
	if (UWorld* World = GetWorld())
	{
		if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
		{
			if (Sky->GetFollowIndex() >= 0)
			{
				Sky->AddFollowZoom(-1.0f);
			}
		}
	}
}

void AGrokExodusSurvivor::OnDrillStarted() { if (TerrainTool) TerrainTool->PrimaryFire(true); }
void AGrokExodusSurvivor::OnDrillCompleted() { if (TerrainTool) TerrainTool->PrimaryFire(false); }
void AGrokExodusSurvivor::OnToolMode() { if (TerrainTool) TerrainTool->CycleMode(); }
void AGrokExodusSurvivor::OnCycleMaterial() { if (TerrainTool) TerrainTool->CyclePlaceMaterial(1); }
void AGrokExodusSurvivor::OnCycleQuality()
{
	if (!TerrainTool) return;
	TerrainTool->DigSpeedMul = (TerrainTool->DigSpeedMul < 1.5f) ? 2.0f : 1.0f;
	TerrainTool->WearMul = 1.0f / TerrainTool->DigSpeedMul;
}

void AGrokExodusSurvivor::OnSaveWorld()
{
	for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
	{
		It->SaveWorld();
		break;
	}
}
