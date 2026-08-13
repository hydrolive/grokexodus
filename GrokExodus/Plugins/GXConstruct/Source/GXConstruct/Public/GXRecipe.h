// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GXItemTypes.h"
#include "GXRecipe.generated.h"

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXRecipeIO
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	int32 Count = 1;
};

UENUM(BlueprintType)
enum class EGXMachineKind : uint8
{
	Refinery,
	Assembler,
	O2H2Generator,
	SurvivalKit
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	FName RecipeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	EGXMachineKind Machine = EGXMachineKind::Assembler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	TArray<FGXRecipeIO> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	TArray<FGXRecipeIO> Outputs;

	/** Seconds at 1.0 productivity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	float Duration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	float PowerW = 500.0f;
};
