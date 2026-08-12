// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 0 pure data-structure harness (also registered as automation test).

#pragma once

#include "CoreMinimal.h"

/**
 * Console / automation harness for Phase 0 validation gate:
 *  1. Create small spherical density field
 *  2. Query points (inside / surface / outside)
 *  3. Deform a region
 *  4. Serialize / deserialize and verify identity
 *
 * Returns true on full pass.
 */
struct FVoxelSmokeTest
{
	/** Run all Phase 0 checks. Logs results to LogVoxelWorld. */
	static bool Run(FString* OutReport = nullptr);

	/** Optional: write a save file under Saved/VoxelSmoke/ for inspection. */
	static bool RunWithFileRoundTrip(const FString& Directory);
};
