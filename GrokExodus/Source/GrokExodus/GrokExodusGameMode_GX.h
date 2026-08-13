// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GrokExodusGameMode_GX.generated.h"

class AGXVoxelWorld;

UCLASS()
class AGXGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGXGameMode();

	virtual void BeginPlay() override;

	/** Playable crust radius (meters). 4 km matches the working prototype scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX")
	float PlanetRadius = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GX")
	float StreamRadius = 180.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	TObjectPtr<AGXVoxelWorld> VoxelWorld;

protected:
	void EnsureWorld();
	void EnsureLighting();
	void PlacePlayerOnSurface();
};
