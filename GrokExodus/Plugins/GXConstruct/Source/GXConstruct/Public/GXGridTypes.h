// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GXBlockDef.h"
#include "GXItemTypes.h"
#include "GXGridTypes.generated.h"

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXBlockInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FName BlockId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FIntVector Coord = FIntVector::ZeroValue;

	/** 0..1. Below 1 the block does not function (SE weld). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float Integrity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	uint8 YawSteps = 0;

	bool IsFunctional() const { return Integrity >= 1.0f; }
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXGridMass
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Grid")
	float MassKg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Grid")
	FVector CoMLocalMeters = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Grid")
	int32 BlockCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Grid")
	int32 PCU = 0;
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXGridData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FName GridId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	EGXGridClass Class = EGXGridClass::Large;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	bool bStatic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	TArray<FGXBlockInstance> Blocks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 PCUCap = 20000;
};
