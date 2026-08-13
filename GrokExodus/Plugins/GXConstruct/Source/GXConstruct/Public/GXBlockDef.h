// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GXItemTypes.h"
#include "GXBlockDef.generated.h"

UENUM(BlueprintType)
enum class EGXGridClass : uint8
{
	Large,
	Small
};

UENUM(BlueprintType)
enum class EGXBlockCategory : uint8
{
	Armor,
	Cockpit,
	Power,
	Thruster,
	Gyro,
	Industry,
	Logistics,
	LifeSupport,
	Tool,
	Landing,
	HeatShield
};

UENUM(BlueprintType)
enum class EGXThrusterKind : uint8
{
	None,
	Atmospheric,
	Hydrogen,
	Ion
};

USTRUCT(BlueprintType)
struct GXCONSTRUCT_API FGXBlockSize
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FIntVector Cells = FIntVector(1, 1, 1);
};

UCLASS(BlueprintType)
class GXCONSTRUCT_API UGXBlockDef : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FName BlockId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	EGXGridClass GridClass = EGXGridClass::Large;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	EGXBlockCategory Category = EGXBlockCategory::Armor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FGXBlockSize Size;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	float MassKg = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	int32 PCU = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	float MaxIntegrity = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float PowerProduceW = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float PowerConsumeW = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float BatteryCapacityJ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight")
	EGXThrusterKind Thruster = EGXThrusterKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight")
	float ThrustN = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight")
	float GyroTorqueNm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat")
	float HeatCapacity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat")
	float MaxTemperatureK = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat")
	bool bAblative = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build")
	TArray<FGXItemStack> BuildComponents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	float InventoryVolumeL = 0.0f;

	static float CellSizeMeters(EGXGridClass Class)
	{
		return Class == EGXGridClass::Large ? 2.5f : 0.5f;
	}
};
