// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VoxelGameMode.generated.h"

class AVoxelPlanetActor;
class AVoxelSunSetup;

/**
 * Spawns the voxel planet and uses AVoxelExodusCharacter as default pawn.
 * Also ensures sun / atmosphere lighting for outdoor planet lighting.
 */
UCLASS()
class AVoxelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVoxelGameMode();

	virtual void BeginPlay() override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** Full design-scale radius (meters). Working set is StreamRadius, not this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	float PlanetRadius = 4000.0f;

	/** Viewer stream radius (meters). Tune for FPS; planet stays large. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	float StreamRadius = 256.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	bool bSpawnPlanetIfMissing = true;

	/** Spawn AVoxelSunSetup if the level has no directional sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Lighting")
	bool bSpawnSunIfMissing = true;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	TObjectPtr<AVoxelPlanetActor> Planet;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel|Lighting")
	TObjectPtr<AVoxelSunSetup> SunSetup;

protected:
	void EnsurePlanet();
	void EnsureLighting();
	void PlacePlayerOnSurface();
};
