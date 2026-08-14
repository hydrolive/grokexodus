// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelPlayerController.h"
#include "GXHUDLayout.h"
#include "GXVersion.h"
#include "GameFramework/HUD.h"
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
	bShowMouseCursor = false;
	ClientSetHUD(AGXHUDLayout::StaticClass());
	if (MyHUD)
	{
		MyHUD->bShowHUD = true;
	}
	ConsoleCommand(TEXT("showhud 1"));
	UE_LOG(LogTemp, Warning, TEXT("********** GX BUILD %s VoxelPC HUD=AGXHUDLayout **********"), GX_VERSION_STRING);
}
