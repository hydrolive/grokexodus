// Copyright Epic Games, Inc. All Rights Reserved.
// Grok Exodus – Voxel World System: core types & constants (Phase 0)

#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

/** Log category for the voxel subsystem. */
DECLARE_LOG_CATEGORY_EXTERN(LogVoxelWorld, Log, All);

/**
 * Density convention:
 *   Density > 0  => solid material
 *   Density < 0  => empty / air
 *   Density == 0 => exact isosurface
 *
 * Material hardness and dig yield are looked up from FVoxelMaterialDef via MaterialId
 * (craftsmanship cascade hooks modify effective dig rate without mutating voxel data).
 */
USTRUCT(BlueprintType)
struct FVoxelCell
{
	GENERATED_BODY()

	/** Signed density. Positive = solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	float Density = -1.0f;

	/** Index into the material table. 0 = air / none. (int32 for BP/UHT; logical range 0–65535) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 MaterialId = 0;

	/** Reserved flags (painted, bunker-protected bit, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 Flags = 0;

	FORCEINLINE bool IsSolid() const { return Density > 0.0f; }
	FORCEINLINE bool IsAir() const { return Density <= 0.0f; }

	static FVoxelCell Air()
	{
		FVoxelCell C;
		C.Density = -1.0f;
		C.MaterialId = 0;
		C.Flags = 0;
		return C;
	}

	static FVoxelCell Solid(int32 InMaterialId, float InDensity = 1.0f)
	{
		FVoxelCell C;
		C.Density = InDensity;
		C.MaterialId = InMaterialId;
		C.Flags = 0;
		return C;
	}
};

/** Voxel flag bits. */
namespace EVoxelFlags
{
	enum Type : int32
	{
		None            = 0,
		PlayerPlaced    = 1 << 0, // player-placed fill (must persist)
		Deformed        = 1 << 1, // density edited from procedural
		BunkerProtected = 1 << 2, // permanent safe-anchor volume
		OreVein         = 1 << 3, // future ore system
		Scarred         = 1 << 4, // AI-war scarring layer
	};
}

/** Integer chunk coordinate in planet-local chunk grid. */
USTRUCT(BlueprintType)
struct FVoxelChunkCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 Z = 0;

	FVoxelChunkCoord() = default;
	FVoxelChunkCoord(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}
	explicit FVoxelChunkCoord(const FIntVector& V) : X(V.X), Y(V.Y), Z(V.Z) {}

	FIntVector ToIntVector() const { return FIntVector(X, Y, Z); }

	bool operator==(const FVoxelChunkCoord& O) const { return X == O.X && Y == O.Y && Z == O.Z; }
	bool operator!=(const FVoxelChunkCoord& O) const { return !(*this == O); }

	friend uint32 GetTypeHash(const FVoxelChunkCoord& C)
	{
		return HashCombine(HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y)), ::GetTypeHash(C.Z));
	}
};

/** Local voxel index inside a chunk [0, ChunkSize). */
USTRUCT(BlueprintType)
struct FVoxelLocalCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	int32 Z = 0;

	FVoxelLocalCoord() = default;
	FVoxelLocalCoord(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}
};

/**
 * Global power-of-two constants for the hierarchical grid.
 * Chunk edge in voxels is always power-of-two for cheap bit ops and LOD parents.
 */
struct FVoxelConstants
{
	/** Voxels along one chunk edge at LOD 0. 32^3 = 32,768 cells ≈ 256 KB dense. */
	static constexpr int32 ChunkSize = 32;
	static constexpr int32 ChunkSizeMask = ChunkSize - 1;
	static constexpr int32 ChunkSizeShift = 5; // log2(32)

	/** Base voxel edge length in meters (planet-local units). */
	static constexpr float BaseVoxelSize = 1.0f;

	/** Number of LOD levels (0 = finest). */
	static constexpr int32 MaxLOD = 5;

	/** Default iteration planet radius (meters). Design scales to larger later. */
	static constexpr float DefaultPlanetRadius = 4000.0f;

	/** Max surface relief amplitude (meters) layered on the base sphere. */
	static constexpr float DefaultMaxRelief = 180.0f;

	/** Crust thickness for surface material layering (meters). */
	static constexpr float DefaultCrustDepth = 12.0f;

	/** Persistence file magic "GXVX". */
	static constexpr uint32 PersistenceMagic = 0x58565847u;
	static constexpr uint32 PersistenceVersion = 1;

	/** Empty / air material id. */
	static constexpr int32 MaterialAir = 0;
};

/** Result of a voxel raycast against density field / mesh colliders. */
USTRUCT(BlueprintType)
struct FVoxelHitResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	bool bHit = false;

	/** World / planet-local hit position. */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	FVector Normal = FVector::UpVector;

	/** Approximate voxel cell coordinate in global voxel indices. */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	FIntVector VoxelCoord = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	int32 MaterialId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	float Distance = 0.0f;
};

/**
 * Parameters passed into dig/place so craftsmanship can cascade later
 * without changing the core density math.
 */
USTRUCT(BlueprintType)
struct FVoxelToolModifiers
{
	GENERATED_BODY()

	/** Multiplier on dig speed (tool quality / craftsmanship). Default 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float DigSpeedMul = 1.0f;

	/** Multiplier on material recovery yield. Default 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float RecoveryMul = 1.0f;

	/** Multiplier on deformation radius precision (higher = cleaner edges). Default 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float PrecisionMul = 1.0f;

	/** Tool wear applied externally; this is informational feedback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	float WearMul = 1.0f;

	/**
	 * When false (default), dig skips BunkerProtected voxels (permanent anchor).
	 * Set true only for admin / deliberate dismantle tools.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craftsmanship")
	bool bBypassBunkerProtection = false;
};

/** Result of a dig operation (feeds inventory / tool wear systems). */
USTRUCT(BlueprintType)
struct FVoxelDigResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	int32 MaterialId = 0;

	/** Volume of solid removed (voxel-units cubed, approximate). */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	float VolumeRemoved = 0.0f;

	/** Yield after RecoveryMul and material DigYield. */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	float YieldAmount = 0.0f;

	/** Suggested tool wear units for the caller to apply. */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	float ToolWear = 0.0f;

	/** Chunks that need remeshing. */
	UPROPERTY(BlueprintReadOnly, Category = "Voxel")
	TArray<FVoxelChunkCoord> DirtyChunks;
};
