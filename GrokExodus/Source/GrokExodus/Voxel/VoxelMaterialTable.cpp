// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelMaterialTable.h"

FVoxelMaterialTable::FVoxelMaterialTable()
{
	ResetToDefaults();
}

void FVoxelMaterialTable::Register(const FVoxelMaterialDef& Def)
{
	Materials.Add(Def.Id, Def);
}

void FVoxelMaterialTable::ResetToDefaults()
{
	Materials.Reset();

	auto Make = [](int32 Id, FName Name, float Hardness, float Yield, float Wear, FLinearColor Color) -> FVoxelMaterialDef
	{
		FVoxelMaterialDef D;
		D.Id = Id;
		D.Name = Name;
		D.Hardness = Hardness;
		D.DigYield = Yield;
		D.WearFactor = Wear;
		D.DebugColor = Color;
		D.PlaceDensity = 1.0f;
		return D;
	};

	auto BindPBR = [](FVoxelMaterialDef& D, const TCHAR* BaseName)
	{
		// Source JPGs live under Content/Voxel/Textures/Source until imported as uassets.
		// Soft paths target the intended cooked asset names after editor import.
		const FString Root = FString::Printf(TEXT("/Game/Voxel/Textures/%s"), BaseName);
		D.Albedo = FSoftObjectPath(FString::Printf(TEXT("%s_A.%s_A"), *Root, BaseName));
		D.Normal = FSoftObjectPath(FString::Printf(TEXT("%s_N.%s_N"), *Root, BaseName));
		D.Roughness = FSoftObjectPath(FString::Printf(TEXT("%s_R.%s_R"), *Root, BaseName));
		D.Metallic = FSoftObjectPath(TEXT("/Game/Voxel/Textures/T_Shared_Metallic.T_Shared_Metallic"));
	};

	// Bright landscape tints so vertex-color terrain never reads as black
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::Air), TEXT("Air"), 0.0f, 0.0f, 0.0f, FLinearColor(0, 0, 0, 0));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::TemperateGrass), TEXT("TemperateGrass"), 0.6f, 0.55f, 0.4f, FLinearColor(0.38f, 0.62f, 0.28f));
		BindPBR(D, TEXT("T_TemperateGrass"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::RockyCliff), TEXT("RockyCliff"), 2.2f, 0.80f, 1.4f, FLinearColor(0.62f, 0.58f, 0.52f));
		BindPBR(D, TEXT("T_RockyCliff"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::DryDirt), TEXT("DryDirt"), 0.9f, 0.70f, 0.6f, FLinearColor(0.68f, 0.48f, 0.28f));
		BindPBR(D, TEXT("T_DryDirt"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::SandCoastal), TEXT("SandCoastal"), 0.5f, 0.65f, 0.35f, FLinearColor(0.88f, 0.78f, 0.52f));
		BindPBR(D, TEXT("T_SandCoastal"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::SnowIce), TEXT("SnowIce"), 0.7f, 0.40f, 0.5f, FLinearColor(0.95f, 0.97f, 1.0f));
		BindPBR(D, TEXT("T_SnowIce"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::WetMud), TEXT("WetMud"), 0.45f, 0.50f, 0.45f, FLinearColor(0.42f, 0.34f, 0.22f));
		BindPBR(D, TEXT("T_WetMud"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::VolcanicScorched), TEXT("VolcanicScorched"), 2.8f, 0.85f, 1.8f, FLinearColor(0.38f, 0.30f, 0.26f));
		BindPBR(D, TEXT("T_VolcanicScorched"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::BedrockDeep), TEXT("BedrockDeep"), 4.0f, 0.90f, 2.5f, FLinearColor(0.42f, 0.42f, 0.46f));
		BindPBR(D, TEXT("T_RockyCliff")); // reuse cliff set for deep bedrock
		Register(D);
	}
	// Phase 8 ores — high yield, high wear
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::OreIron), TEXT("OreIron"), 2.6f, 1.35f, 1.6f, FLinearColor(0.55f, 0.42f, 0.38f));
		BindPBR(D, TEXT("T_RockyCliff"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::OreCopper), TEXT("OreCopper"), 2.0f, 1.45f, 1.3f, FLinearColor(0.72f, 0.45f, 0.22f));
		BindPBR(D, TEXT("T_DryDirt"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::OreCrystal), TEXT("OreCrystal"), 3.2f, 1.80f, 2.0f, FLinearColor(0.45f, 0.75f, 0.95f));
		BindPBR(D, TEXT("T_SnowIce"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::Concrete), TEXT("Concrete"), 2.4f, 0.30f, 1.2f, FLinearColor(0.55f, 0.55f, 0.52f));
		BindPBR(D, TEXT("T_RockyCliff"));
		Register(D);
	}
	{
		FVoxelMaterialDef D = Make(static_cast<int32>(EVoxelMaterialId::BunkerLiner), TEXT("BunkerLiner"), 3.5f, 0.15f, 1.8f, FLinearColor(0.35f, 0.40f, 0.38f));
		BindPBR(D, TEXT("T_RockyCliff"));
		Register(D);
	}
}

const FVoxelMaterialDef* FVoxelMaterialTable::Find(int32 MaterialId) const
{
	return Materials.Find(MaterialId);
}

FVoxelMaterialDef& FVoxelMaterialTable::FindOrAdd(int32 MaterialId)
{
	if (FVoxelMaterialDef* Existing = Materials.Find(MaterialId))
	{
		return *Existing;
	}
	FVoxelMaterialDef& NewDef = Materials.Add(MaterialId);
	NewDef.Id = MaterialId;
	NewDef.Name = *FString::Printf(TEXT("Material_%d"), MaterialId);
	return NewDef;
}

float FVoxelMaterialTable::GetHardness(int32 MaterialId) const
{
	if (const FVoxelMaterialDef* D = Find(MaterialId))
	{
		return D->Hardness;
	}
	return 1.0f;
}

float FVoxelMaterialTable::GetDigYield(int32 MaterialId) const
{
	if (const FVoxelMaterialDef* D = Find(MaterialId))
	{
		return D->DigYield;
	}
	return 0.5f;
}

float FVoxelMaterialTable::GetWearFactor(int32 MaterialId) const
{
	if (const FVoxelMaterialDef* D = Find(MaterialId))
	{
		return D->WearFactor;
	}
	return 1.0f;
}

FLinearColor FVoxelMaterialTable::GetDebugColor(int32 MaterialId) const
{
	if (const FVoxelMaterialDef* D = Find(MaterialId))
	{
		return D->DebugColor;
	}
	return FLinearColor::Gray;
}

float FVoxelMaterialTable::ComputeDigRate(float Hardness, const FVoxelToolModifiers& Tool)
{
	const float H = FMath::Max(Hardness, 0.05f);
	return FMath::Max(Tool.DigSpeedMul, 0.0f) / H;
}

float FVoxelMaterialTable::ComputeYield(float VolumeRemoved, float DigYield, const FVoxelToolModifiers& Tool)
{
	return FMath::Max(0.0f, VolumeRemoved * DigYield * Tool.RecoveryMul);
}

float FVoxelMaterialTable::ComputeWear(float VolumeRemoved, float WearFactor, const FVoxelToolModifiers& Tool)
{
	return FMath::Max(0.0f, VolumeRemoved * WearFactor * Tool.WearMul);
}
