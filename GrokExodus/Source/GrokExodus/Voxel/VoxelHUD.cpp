// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelHUD.h"

void AVoxelHUD::DrawHUD()
{
	Super::DrawHUD();
	// Boot UI lives on the Slate viewport overlay (UGXBootOverlaySubsystem).
	// Do not call GXLoadScreen from the game module — those symbols are plugin-private.
}
