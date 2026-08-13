// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GXItemTypes.generated.h"

UENUM(BlueprintType)
enum class EGXItemCategory : uint8
{
	Ore,
	Ingot,
	Component,
	Tool,
	Consumable,
	Block
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 Count = 0;

	/** 0..1 for damaged components / partial welds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float Integrity = 1.0f;

	bool IsEmpty() const { return ItemId.IsNone() || Count <= 0; }

	float GetMassKg(float UnitMassKg) const
	{
		return UnitMassKg * static_cast<float>(FMath::Max(Count, 0));
	}
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXItemDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EGXItemCategory Category = EGXItemCategory::Ore;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float MassKg = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 MaxStack = 1000;
};
