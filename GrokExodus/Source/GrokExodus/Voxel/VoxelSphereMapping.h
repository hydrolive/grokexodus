// Copyright Epic Games, Inc. All Rights Reserved.
// Spherical density field + coordinate helpers (cartesian sparse grid over sphere).

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelNoise.h"
#include "Voxel/VoxelMaterialTable.h"

/**
 * Spherical mapping strategy (chosen for Phase 0):
 *
 *   Cartesian sparse chunk grid centered on the planet origin.
 *   Density is a radial signed field: base radius + continental / mountain noise
 *   evaluated on the unit sphere direction, minus sample radius.
 *
 * Rationale vs cube-to-sphere multi-grid:
 *   - Single address space for dig tunnels through the interior (no face seams).
 *   - Private bunkers and walkers share one coordinate frame.
 *   - LOD is simple power-of-two chunk hierarchy.
 *   - Distortion is in noise sampling (mitigated by sampling on unit direction),
 *     not in voxel connectivity — critical for volumetric editing.
 *
 * Precision:
 *   - Authoritative planet-local positions use double (FVector3d) when converting
 *     large indices; mesh generation uses float32 in chunk-local space.
 *   - Origin shifting (player-centric float world) is applied by the game layer.
 */
struct FVoxelPlanetParams
{
	float Radius = FVoxelConstants::DefaultPlanetRadius;
	float MaxRelief = FVoxelConstants::DefaultMaxRelief;
	float CrustDepth = FVoxelConstants::DefaultCrustDepth;
	float VoxelSize = FVoxelConstants::BaseVoxelSize;
	uint32 Seed = 1337u;

	/** Continent scale (noise frequency on unit sphere). */
	float ContinentFreq = 2.5f;
	float MountainFreq = 8.0f;
	float DetailFreq = 24.0f;

	/** Sea level offset relative to Radius (negative = more ocean later). */
	float SeaLevelBias = 0.0f;
};

/**
 * Pure procedural sampler — source of truth for unedited space.
 */
class FVoxelSphereMapping
{
public:
	explicit FVoxelSphereMapping(const FVoxelPlanetParams& InParams = FVoxelPlanetParams())
		: Params(InParams)
	{
	}

	const FVoxelPlanetParams& GetParams() const { return Params; }
	void SetParams(const FVoxelPlanetParams& InParams) { Params = InParams; }

	/** Gravity direction at a planet-local position (toward center). */
	static FVector GravityDirection(const FVector& PlanetLocalPos)
	{
		const double LenSq = static_cast<double>(PlanetLocalPos.X) * PlanetLocalPos.X
			+ static_cast<double>(PlanetLocalPos.Y) * PlanetLocalPos.Y
			+ static_cast<double>(PlanetLocalPos.Z) * PlanetLocalPos.Z;
		if (LenSq < 1e-12)
		{
			return FVector(0.0f, 0.0f, -1.0f);
		}
		const double InvLen = 1.0 / FMath::Sqrt(LenSq);
		return FVector(
			static_cast<float>(-PlanetLocalPos.X * InvLen),
			static_cast<float>(-PlanetLocalPos.Y * InvLen),
			static_cast<float>(-PlanetLocalPos.Z * InvLen));
	}

	/** Up = away from center. */
	static FVector UpDirection(const FVector& PlanetLocalPos)
	{
		return -GravityDirection(PlanetLocalPos);
	}

	/** Surface height displacement along radial direction (meters). */
	float SampleHeightDisplacement(const FVector& UnitDir) const
	{
		const float Ux = UnitDir.X;
		const float Uy = UnitDir.Y;
		const float Uz = UnitDir.Z;

		// Continents: large low-freq blobs → land vs lowland
		const float Continents = FVoxelNoise::FBm(
			Ux * Params.ContinentFreq,
			Uy * Params.ContinentFreq,
			Uz * Params.ContinentFreq,
			Params.Seed,
			5, 2.0f, 0.5f);

		// Mountains: ridged overlay
		const float Mountains = FVoxelNoise::Ridged(
			Ux * Params.MountainFreq,
			Uy * Params.MountainFreq,
			Uz * Params.MountainFreq,
			Params.Seed + 7u,
			4);

		// Fine detail
		const float Detail = FVoxelNoise::FBm(
			Ux * Params.DetailFreq,
			Uy * Params.DetailFreq,
			Uz * Params.DetailFreq,
			Params.Seed + 19u,
			3, 2.0f, 0.5f);

		// Shape continents so ~55% of sphere is elevated land
		const float LandMask = FMath::Clamp((Continents + 0.15f) * 1.4f, 0.0f, 1.0f);
		const float Relief = LandMask * (0.55f + 0.45f * Mountains) + Detail * 0.08f * LandMask;

		return Relief * Params.MaxRelief + Params.SeaLevelBias;
	}

	/**
	 * Signed density at planet-local position (meters).
	 * Positive inside solid crust / mantle sample.
	 */
	float SampleDensity(const FVector& PlanetLocalPos) const
	{
		const double X = PlanetLocalPos.X;
		const double Y = PlanetLocalPos.Y;
		const double Z = PlanetLocalPos.Z;
		const double R = FMath::Sqrt(X * X + Y * Y + Z * Z);

		if (R < 1e-6)
		{
			// Planet core: solid
			return Params.Radius;
		}

		const FVector UnitDir(
			static_cast<float>(X / R),
			static_cast<float>(Y / R),
			static_cast<float>(Z / R));

		const float SurfaceR = Params.Radius + SampleHeightDisplacement(UnitDir);
		// Density = distance inside surface (positive below surface)
		return static_cast<float>(SurfaceR - R);
	}

	/**
	 * Material for a solid sample based on depth below surface and latitude.
	 */
	int32 SampleMaterial(const FVector& PlanetLocalPos, float Density) const
	{
		if (Density <= 0.0f)
		{
			return FVoxelConstants::MaterialAir;
		}

		const double X = PlanetLocalPos.X;
		const double Y = PlanetLocalPos.Y;
		const double Z = PlanetLocalPos.Z;
		const double R = FMath::Sqrt(X * X + Y * Y + Z * Z);
		if (R < 1e-6)
		{
			return static_cast<int32>(EVoxelMaterialId::BedrockDeep);
		}

		const FVector UnitDir(
			static_cast<float>(X / R),
			static_cast<float>(Y / R),
			static_cast<float>(Z / R));

		const float SurfaceR = Params.Radius + SampleHeightDisplacement(UnitDir);
		const float Depth = SurfaceR - static_cast<float>(R); // == Density for pure procedural
		const float Latitude = FMath::Abs(UnitDir.Z); // 0 equator, 1 pole (Z-up planet)

		// Deep interior
		if (Depth > Params.CrustDepth * 3.0f)
		{
			return static_cast<int32>(EVoxelMaterialId::BedrockDeep);
		}
		if (Depth > Params.CrustDepth)
		{
			return static_cast<int32>(EVoxelMaterialId::RockyCliff);
		}

		// Surface materials
		const float HeightAboveBase = SurfaceR - Params.Radius;

		if (Latitude > 0.78f || HeightAboveBase > Params.MaxRelief * 0.72f)
		{
			return static_cast<int32>(EVoxelMaterialId::SnowIce);
		}

		// Subtle scar noise for future AI-war integration (sparse volcanic patches)
		const float Scar = FVoxelNoise::FBm(UnitDir.X * 12.0f, UnitDir.Y * 12.0f, UnitDir.Z * 12.0f, Params.Seed + 99u, 3);
		if (Scar > 0.82f && HeightAboveBase < Params.MaxRelief * 0.2f)
		{
			return static_cast<int32>(EVoxelMaterialId::VolcanicScorched);
		}

		if (HeightAboveBase < Params.MaxRelief * 0.05f)
		{
			// Lowlands: mud / sand bands
			const float Coast = FVoxelNoise::ValueNoise3D(UnitDir.X * 20.0f, UnitDir.Y * 20.0f, UnitDir.Z * 20.0f, Params.Seed + 3u);
			if (Coast > 0.25f)
			{
				return static_cast<int32>(EVoxelMaterialId::SandCoastal);
			}
			return static_cast<int32>(EVoxelMaterialId::WetMud);
		}

		if (HeightAboveBase > Params.MaxRelief * 0.35f)
		{
			return static_cast<int32>(EVoxelMaterialId::RockyCliff);
		}

		// Mid elevations: grass vs dry dirt
		const float Dry = FVoxelNoise::ValueNoise3D(UnitDir.X * 15.0f, UnitDir.Y * 15.0f, UnitDir.Z * 15.0f, Params.Seed + 11u);
		if (Dry > 0.35f)
		{
			return static_cast<int32>(EVoxelMaterialId::DryDirt);
		}
		return static_cast<int32>(EVoxelMaterialId::TemperateGrass);
	}

	/** Full procedural cell at planet-local position. */
	FVoxelCell SampleCell(const FVector& PlanetLocalPos) const
	{
		const float D = SampleDensity(PlanetLocalPos);
		if (D <= 0.0f)
		{
			return FVoxelCell::Air();
		}
		return FVoxelCell::Solid(SampleMaterial(PlanetLocalPos, D), D);
	}

	// ---- Coordinate conversion (global voxel indices ↔ planet-local meters) ----

	/** Voxel size at a given LOD (LOD0 = base). */
	float VoxelSizeAtLOD(int32 LOD) const
	{
		return Params.VoxelSize * static_cast<float>(1 << FMath::Clamp(LOD, 0, FVoxelConstants::MaxLOD));
	}

	float ChunkWorldSize(int32 LOD = 0) const
	{
		return VoxelSizeAtLOD(LOD) * static_cast<float>(FVoxelConstants::ChunkSize);
	}

	/** Convert planet-local position to global voxel coordinate at LOD0. */
	FIntVector WorldToVoxel(const FVector& PlanetLocalPos) const
	{
		const float Inv = 1.0f / Params.VoxelSize;
		return FIntVector(
			FMath::FloorToInt(PlanetLocalPos.X * Inv),
			FMath::FloorToInt(PlanetLocalPos.Y * Inv),
			FMath::FloorToInt(PlanetLocalPos.Z * Inv));
	}

	/** Center of a LOD0 voxel cell in planet-local meters. */
	FVector VoxelToWorldCenter(const FIntVector& VoxelCoord) const
	{
		const float S = Params.VoxelSize;
		return FVector(
			(static_cast<float>(VoxelCoord.X) + 0.5f) * S,
			(static_cast<float>(VoxelCoord.Y) + 0.5f) * S,
			(static_cast<float>(VoxelCoord.Z) + 0.5f) * S);
	}

	/** Corner of a LOD0 voxel cell (min corner). */
	FVector VoxelToWorldMin(const FIntVector& VoxelCoord) const
	{
		const float S = Params.VoxelSize;
		return FVector(
			static_cast<float>(VoxelCoord.X) * S,
			static_cast<float>(VoxelCoord.Y) * S,
			static_cast<float>(VoxelCoord.Z) * S);
	}

	static FVoxelChunkCoord VoxelToChunk(const FIntVector& VoxelCoord)
	{
		auto DivFloor = [](int32 V) -> int32
		{
			// Floor division for negative coords
			if (V >= 0)
			{
				return V >> FVoxelConstants::ChunkSizeShift;
			}
			return -((-V + FVoxelConstants::ChunkSizeMask) >> FVoxelConstants::ChunkSizeShift);
		};
		return FVoxelChunkCoord(DivFloor(VoxelCoord.X), DivFloor(VoxelCoord.Y), DivFloor(VoxelCoord.Z));
	}

	static FVoxelLocalCoord VoxelToLocal(const FIntVector& VoxelCoord)
	{
		auto Mod = [](int32 V) -> int32
		{
			const int32 M = V % FVoxelConstants::ChunkSize;
			return M < 0 ? M + FVoxelConstants::ChunkSize : M;
		};
		return FVoxelLocalCoord(Mod(VoxelCoord.X), Mod(VoxelCoord.Y), Mod(VoxelCoord.Z));
	}

	static FIntVector ChunkLocalToVoxel(const FVoxelChunkCoord& Chunk, int32 LX, int32 LY, int32 LZ)
	{
		return FIntVector(
			Chunk.X * FVoxelConstants::ChunkSize + LX,
			Chunk.Y * FVoxelConstants::ChunkSize + LY,
			Chunk.Z * FVoxelConstants::ChunkSize + LZ);
	}

	FVector ChunkOriginWorld(const FVoxelChunkCoord& Chunk, int32 LOD = 0) const
	{
		const float CS = ChunkWorldSize(LOD);
		return FVector(
			static_cast<float>(Chunk.X) * CS,
			static_cast<float>(Chunk.Y) * CS,
			static_cast<float>(Chunk.Z) * CS);
	}

private:
	FVoxelPlanetParams Params;
};
