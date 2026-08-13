// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelPlayerController.h"
#include "GXHUDLayout.h"
#include "GXVersion.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"

AVoxelPlayerController::AVoxelPlayerController()
{
	bShowMouseCursor = false;

	// Wire template Enhanced Input contexts so pure C++ pawn can move/look.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCDefault(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (IMCDefault.Succeeded())
	{
		DefaultMappingContexts.Add(IMCDefault.Object);
	}
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCMouse(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	if (IMCMouse.Succeeded())
	{
		MobileExcludedMappingContexts.Add(IMCMouse.Object);
	}
}

void AVoxelPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ClientSetHUD(AGXHUDLayout::StaticClass());
	UE_LOG(LogTemp, Warning, TEXT("********** GX BUILD %s VoxelPC HUD=AGXHUDLayout **********"), GX_VERSION_STRING);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(8, 25.f, FColor::Cyan,
			FString::Printf(TEXT("GX %s HUD forced"), GX_VERSION_STRING));
	}
}
