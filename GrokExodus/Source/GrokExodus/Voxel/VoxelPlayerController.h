// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GrokExodusPlayerController.h"
#include "VoxelPlayerController.generated.h"

/** Optional PC subclass for voxel sessions (mouse look defaults). */
UCLASS()
class AVoxelPlayerController : public AGrokExodusPlayerController
{
	GENERATED_BODY()

public:
	AVoxelPlayerController();
};
