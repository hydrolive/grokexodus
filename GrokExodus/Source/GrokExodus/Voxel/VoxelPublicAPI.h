// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 6 – clean extension API surface for craftsmanship, bunkers, walkers.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelPlanetActor.h"

/**
 * Facade helpers for other game systems.
 * Prefer these over reaching into FVoxelVolume internals.
 */
namespace VoxelAPI
{
	/** Craftsmanship: build modifiers from tool quality [0..] and optional bonuses. */
	inline FVoxelToolModifiers MakeToolModifiers(float ToolQuality, float RecoveryBonus = 0.0f, float PrecisionBonus = 0.0f)
	{
		FVoxelToolModifiers M;
		// Quality 1 = baseline; quality 2 = double dig speed, etc.
		M.DigSpeedMul = FMath::Max(0.05f, ToolQuality);
		M.RecoveryMul = FMath::Max(0.0f, 1.0f + RecoveryBonus);
		M.PrecisionMul = FMath::Max(0.25f, 1.0f + PrecisionBonus);
		M.WearMul = FMath::Max(0.05f, 1.0f / FMath::Max(ToolQuality, 0.25f));
		return M;
	}

	/** Register a private bunker AABB (world cm) so chunks stay resident + flagged. */
	inline void RegisterPrivateBunker(AVoxelPlanetActor* Planet, const FVector& WorldCenter, const FVector& HalfExtentsCm)
	{
		if (Planet)
		{
			Planet->RegisterBunkerVolumeWorld(WorldCenter, HalfExtentsCm);
		}
	}

	/** Walker / vehicle collision probe: true if solid density at world position. */
	inline bool IsSolidAt(AVoxelPlanetActor* Planet, const FVector& WorldPos)
	{
		return Planet && Planet->SampleDensityWorld(WorldPos) > 0.0f;
	}

	/** Sphere overlap solid test for walker foot/wheel samples. */
	inline bool SphereHitsTerrain(AVoxelPlanetActor* Planet, const FVector& WorldCenter, float RadiusCm, int32 Samples = 8)
	{
		if (!Planet || RadiusCm <= 0.0f)
		{
			return false;
		}
		if (IsSolidAt(Planet, WorldCenter))
		{
			return true;
		}
		for (int32 I = 0; I < Samples; ++I)
		{
			const float A = (2.0f * PI * I) / Samples;
			const FVector Offset(FMath::Cos(A) * RadiusCm, FMath::Sin(A) * RadiusCm, 0.0f);
			if (IsSolidAt(Planet, WorldCenter + Offset))
			{
				return true;
			}
		}
		return IsSolidAt(Planet, WorldCenter - FVector(0, 0, RadiusCm));
	}

	/** Persist planet deformations (bunker permanence). */
	inline bool SaveWorld(AVoxelPlanetActor* Planet)
	{
		return Planet && Planet->SavePlanet();
	}

	inline bool LoadWorld(AVoxelPlanetActor* Planet)
	{
		return Planet && Planet->LoadPlanet();
	}
}
