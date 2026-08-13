// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelCraftsmanshipComponent.h"
#include "Voxel/VoxelPublicAPI.h"

UVoxelCraftsmanshipComponent::UVoxelCraftsmanshipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVoxelToolModifiers UVoxelCraftsmanshipComponent::MakeModifiers() const
{
	const float Q = FMath::Max(0.25f, ToolQuality);
	const float DurFrac = MaxDurability > 0.f ? FMath::Clamp(ToolDurability / MaxDurability, 0.05f, 1.f) : 1.f;
	// Broken tools dig very slowly
	const float EffectiveQ = Q * (0.15f + 0.85f * DurFrac);
	return VoxelAPI::MakeToolModifiers(EffectiveQ, 0.0f, 0.0f);
}

void UVoxelCraftsmanshipComponent::ApplyDigResult(const FVoxelDigResult& Result)
{
	if (!Result.bSuccess)
	{
		return;
	}
	if (Result.YieldAmount > 0.f && Result.MaterialId > 0)
	{
		MaterialStock.FindOrAdd(Result.MaterialId) += Result.YieldAmount;
	}
	ToolDurability = FMath::Max(0.f, ToolDurability - Result.ToolWear);
}

void UVoxelCraftsmanshipComponent::CycleToolQuality(int32 Direction)
{
	static const float Tiers[] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
	constexpr int32 N = 5;
	int32 Idx = 1;
	for (int32 I = 0; I < N; ++I)
	{
		if (FMath::IsNearlyEqual(ToolQuality, Tiers[I], 0.05f))
		{
			Idx = I;
			break;
		}
	}
	Idx = FMath::Clamp(Idx + Direction, 0, N - 1);
	ToolQuality = Tiers[Idx];
	UE_LOG(LogVoxelWorld, Log, TEXT("Tool quality set to %.2f"), ToolQuality);
}

void UVoxelCraftsmanshipComponent::RepairTool(float Amount)
{
	ToolDurability = FMath::Min(MaxDurability, ToolDurability + Amount);
}

float UVoxelCraftsmanshipComponent::GetStock(int32 MaterialId) const
{
	if (const float* V = MaterialStock.Find(MaterialId))
	{
		return *V;
	}
	return 0.f;
}

float UVoxelCraftsmanshipComponent::GetTotalStock() const
{
	float Sum = 0.f;
	for (const auto& P : MaterialStock)
	{
		Sum += P.Value;
	}
	return Sum;
}

FString UVoxelCraftsmanshipComponent::GetStatusLine() const
{
	return FString::Printf(TEXT("Tool Q=%.1f Dur=%.0f/%.0f  Stock=%.2f m3"),
		ToolQuality, ToolDurability, MaxDurability, GetTotalStock());
}
