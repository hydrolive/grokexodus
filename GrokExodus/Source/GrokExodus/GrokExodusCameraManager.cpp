// Copyright Epic Games, Inc. All Rights Reserved.


#include "GrokExodusCameraManager.h"

AGrokExodusCameraManager::AGrokExodusCameraManager()
{
	// Full FPS pitch range (voxel character owns look; manager clamps still apply to some paths)
	ViewPitchMin = -89.0f;
	ViewPitchMax = 89.0f;
}
