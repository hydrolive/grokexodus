// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** 4-byte packed cell. Unedited space is not stored. */
struct FGXVoxelPacked
{
	int16 Density = 0;
	uint8 Material = 0;
	uint8 Flags = 0;

	static constexpr float DensityScale = 1024.0f; // ±32 m at ~1 mm
	static constexpr float MaxAbsDensityM = 32.0f;

	FORCEINLINE bool IsSolid() const { return Density > 0; }

	static FGXVoxelPacked MakeAir()
	{
		FGXVoxelPacked C;
		C.Density = -static_cast<int16>(DensityScale);
		C.Material = 0;
		C.Flags = 0;
		return C;
	}

	static FGXVoxelPacked FromDensity(float DensityMeters, uint8 InMaterial, uint8 InFlags = 0)
	{
		FGXVoxelPacked C;
		const float Clamped = FMath::Clamp(DensityMeters, -MaxAbsDensityM, MaxAbsDensityM);
		C.Density = static_cast<int16>(FMath::RoundToInt(Clamped * DensityScale));
		C.Material = InMaterial;
		C.Flags = InFlags;
		return C;
	}

	FORCEINLINE float ToDensityMeters() const
	{
		return static_cast<float>(Density) / DensityScale;
	}
};

static_assert(sizeof(FGXVoxelPacked) == 4, "Packed voxel must be 4 bytes");

namespace EGXVoxelFlags
{
	enum Type : uint8
	{
		None         = 0,
		PlayerPlaced = 1 << 0,
		Deformed     = 1 << 1,
		OreVein      = 1 << 2,
		Scarred      = 1 << 3,
		Liquid       = 1 << 4,
	};
}

enum class EGXVoxelMaterial : uint8
{
	Air              = 0,
	TemperateGrass   = 1,
	RockyCliff       = 2,
	DryDirt          = 3,
	SandCoastal      = 4,
	SnowIce          = 5,
	WetMud           = 6,
	VolcanicScorched = 7,
	BedrockDeep      = 8,
	OreIron          = 9,
	OreCopper        = 10,
	OreCrystal       = 11,
	Concrete         = 12,
	Count
};

inline FLinearColor GXMaterialDebugColor(int32 MaterialId)
{
	switch (MaterialId)
	{
	case 1:  return FLinearColor(0.38f, 0.62f, 0.28f);
	case 2:  return FLinearColor(0.62f, 0.58f, 0.52f);
	case 3:  return FLinearColor(0.68f, 0.48f, 0.28f);
	case 4:  return FLinearColor(0.88f, 0.78f, 0.52f);
	case 5:  return FLinearColor(0.95f, 0.97f, 1.0f);
	case 6:  return FLinearColor(0.42f, 0.34f, 0.22f);
	case 7:  return FLinearColor(0.38f, 0.30f, 0.26f);
	case 8:  return FLinearColor(0.42f, 0.42f, 0.46f);
	case 9:  return FLinearColor(0.55f, 0.42f, 0.38f);
	case 10: return FLinearColor(0.72f, 0.45f, 0.22f);
	case 11: return FLinearColor(0.45f, 0.75f, 0.95f);
	case 12: return FLinearColor(0.55f, 0.55f, 0.52f);
	default: return FLinearColor(0.55f, 0.55f, 0.50f);
	}
}

struct FGXVoxelConstants
{
	static constexpr int32 ChunkSize = 32;
	static constexpr int32 ChunkShift = 5;
	static constexpr int32 PageSize = 8;
	static constexpr int32 PageShift = 3;
	static constexpr int32 PagesPerAxis = ChunkSize / PageSize; // 4
	static constexpr int32 PagesPerChunk = PagesPerAxis * PagesPerAxis * PagesPerAxis; // 64
	static constexpr int32 CellsPerPage = PageSize * PageSize * PageSize; // 512
	static constexpr float DefaultVoxelSize = 1.0f;
};

struct FGXChunkKey
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;

	FGXChunkKey() = default;
	FGXChunkKey(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}

	bool operator==(const FGXChunkKey& O) const { return X == O.X && Y == O.Y && Z == O.Z; }

	friend uint32 GetTypeHash(const FGXChunkKey& K)
	{
		return HashCombine(HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y)), ::GetTypeHash(K.Z));
	}
};

struct FGXPageKey
{
	int32 X = 0; // 0..3 inside chunk
	int32 Y = 0;
	int32 Z = 0;

	FORCEINLINE int32 Index() const
	{
		return X + FGXVoxelConstants::PagesPerAxis * (Y + FGXVoxelConstants::PagesPerAxis * Z);
	}
};
