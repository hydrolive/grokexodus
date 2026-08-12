// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelPlayerController.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"

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
