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

	// ---- Phase 8 world layers ----
	/** Moisture noise frequency on unit sphere (biomes). */
	float MoistureFreq = 3.5f;
	/** Ore vein frequency (higher = smaller pockets). */
	float OreFreq = 18.0f;
	/** AI-war scar frequency. */
	float ScarFreq = 9.0f;
	/** Max crater depth carved by scars (meters). */
	float ScarMaxDepth = 28.0f;
	/** Threshold [0,1] for scar activation (higher = rarer). */
	float ScarThreshold = 0.78f;
	/** Ore density threshold (higher = rarer veins). */
	float OreThreshold = 0.72f;
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

	/** AI-war scar strength [0,1] on unit direction. */
	float SampleScarStrength(const FVector& UnitDir) const
	{
		const float N = FVoxelNoise::FBm(
			UnitDir.X * Params.ScarFreq,
			UnitDir.Y * Params.ScarFreq,
			UnitDir.Z * Params.ScarFreq,
			Params.Seed + 99u, 4);
		const float T = (N - Params.ScarThreshold) / FMath::Max(1.0f - Params.ScarThreshold, 0.05f);
		return FMath::Clamp(T, 0.0f, 1.0f);
	}

	/** Meters carved from surface by scars / impact craters. */
	float SampleScarCarveMeters(const FVector& UnitDir) const
	{
		const float S = SampleScarStrength(UnitDir);
		if (S <= 0.0f)
		{
			return 0.0f;
		}
		// Ridged center for bowl-shaped craters
		const float Bowl = FVoxelNoise::Ridged(
			UnitDir.X * Params.ScarFreq * 1.7f,
			UnitDir.Y * Params.ScarFreq * 1.7f,
			UnitDir.Z * Params.ScarFreq * 1.7f,
			Params.Seed + 140u, 2);
		return S * Params.ScarMaxDepth * (0.45f + 0.55f * Bowl);
	}

	/** Moisture [0,1] for biome selection. */
	float SampleMoisture(const FVector& UnitDir) const
	{
		const float M = FVoxelNoise::FBm(
			UnitDir.X * Params.MoistureFreq,
			UnitDir.Y * Params.MoistureFreq,
			UnitDir.Z * Params.MoistureFreq,
			Params.Seed + 33u, 4);
		return FMath::Clamp(M * 0.5f + 0.5f, 0.0f, 1.0f);
	}

	/**
	 * Signed density at planet-local position (meters).
	 * Positive inside solid crust / mantle sample.
	 * Phase 8: scar craters lower the surface (carve into crust).
	 */
	float SampleDensity(const FVector& PlanetLocalPos) const
	{
		const double X = PlanetLocalPos.X;
		const double Y = PlanetLocalPos.Y;
		const double Z = PlanetLocalPos.Z;
		const double R = FMath::Sqrt(X * X + Y * Y + Z * Z);

		if (R < 1e-6)
		{
			return Params.Radius;
		}

		const FVector UnitDir(
			static_cast<float>(X / R),
			static_cast<float>(Y / R),
			static_cast<float>(Z / R));

		const float SurfaceR = Params.Radius + SampleHeightDisplacement(UnitDir) - SampleScarCarveMeters(UnitDir);
		return static_cast<float>(SurfaceR - R);
	}

	/**
	 * Faster density for meshing (fewer noise octaves). Includes scar carve.
	 */
	float SampleDensityFast(const FVector& PlanetLocalPos) const
	{
		const double X = PlanetLocalPos.X;
		const double Y = PlanetLocalPos.Y;
		const double Z = PlanetLocalPos.Z;
		const double R = FMath::Sqrt(X * X + Y * Y + Z * Z);
		if (R < 1e-6)
		{
			return Params.Radius;
		}
		const float Ux = static_cast<float>(X / R);
		const float Uy = static_cast<float>(Y / R);
		const float Uz = static_cast<float>(Z / R);
		const FVector UnitDir(Ux, Uy, Uz);

		const float Continents = FVoxelNoise::FBm(
			Ux * Params.ContinentFreq, Uy * Params.ContinentFreq, Uz * Params.ContinentFreq,
			Params.Seed, 3, 2.0f, 0.5f);
		const float Mountains = FVoxelNoise::Ridged(
			Ux * Params.MountainFreq, Uy * Params.MountainFreq, Uz * Params.MountainFreq,
			Params.Seed + 7u, 2);
		const float Detail = FVoxelNoise::FBm(
			Ux * Params.DetailFreq, Uy * Params.DetailFreq, Uz * Params.DetailFreq,
			Params.Seed + 19u, 2, 2.0f, 0.5f);

		const float LandMask = FMath::Clamp((Continents + 0.15f) * 1.4f, 0.0f, 1.0f);
		const float Relief = LandMask * (0.55f + 0.45f * Mountains) + Detail * 0.08f * LandMask;
		const float SurfaceR = Params.Radius + Relief * Params.MaxRelief + Params.SeaLevelBias
			- SampleScarCarveMeters(UnitDir);
		return static_cast<float>(SurfaceR - R);
	}

	/**
	 * Material for a solid sample: biomes + depth + ores + scars (Phase 8).
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

		const float BaseDisp = SampleHeightDisplacement(UnitDir);
		const float ScarCarve = SampleScarCarveMeters(UnitDir);
		const float SurfaceR = Params.Radius + BaseDisp - ScarCarve;
		const float Depth = SurfaceR - static_cast<float>(R);
		const float Latitude = FMath::Abs(UnitDir.Z);
		const float Moisture = SampleMoisture(UnitDir);
		const float HeightAboveBase = BaseDisp - ScarCarve;

		// Deep interior
		if (Depth > Params.CrustDepth * 3.5f)
		{
			return static_cast<int32>(EVoxelMaterialId::BedrockDeep);
		}

		// Ore veins in mid-crust (Phase 8)
		if (Depth > Params.CrustDepth * 0.35f && Depth < Params.CrustDepth * 2.8f)
		{
			const float OreN = FVoxelNoise::FBm(
				UnitDir.X * Params.OreFreq + Depth * 0.15f,
				UnitDir.Y * Params.OreFreq,
				UnitDir.Z * Params.OreFreq + static_cast<float>(R) * 0.02f,
				Params.Seed + 201u, 4);
			if (OreN > Params.OreThreshold)
			{
				// Type by latitude / moisture
				if (Latitude > 0.55f || Moisture < 0.35f)
				{
					return static_cast<int32>(EVoxelMaterialId::OreIron);
				}
				if (Moisture > 0.65f)
				{
					return static_cast<int32>(EVoxelMaterialId::OreCrystal);
				}
				return static_cast<int32>(EVoxelMaterialId::OreCopper);
			}
		}

		if (Depth > Params.CrustDepth)
		{
			return static_cast<int32>(EVoxelMaterialId::RockyCliff);
		}

		// Scar surface — scorched / glass
		if (ScarCarve > 2.0f)
		{
			return static_cast<int32>(EVoxelMaterialId::VolcanicScorched);
		}

		// Biomes by latitude + moisture + elevation
		if (Latitude > 0.78f || HeightAboveBase > Params.MaxRelief * 0.72f)
		{
			return static_cast<int32>(EVoxelMaterialId::SnowIce);
		}

		if (HeightAboveBase < Params.MaxRelief * 0.05f)
		{
			if (Moisture > 0.55f)
			{
				return static_cast<int32>(EVoxelMaterialId::WetMud);
			}
			return static_cast<int32>(EVoxelMaterialId::SandCoastal);
		}

		if (HeightAboveBase > Params.MaxRelief * 0.35f)
		{
			return static_cast<int32>(EVoxelMaterialId::RockyCliff);
		}

		// Mid elevations: wet → grass, dry → dirt
		if (Moisture < 0.40f)
		{
			return static_cast<int32>(EVoxelMaterialId::DryDirt);
		}
		return static_cast<int32>(EVoxelMaterialId::TemperateGrass);
	}

	/** Full procedural cell: density + material + ore/scar flags. */
	FVoxelCell SampleCell(const FVector& PlanetLocalPos) const
	{
		const float D = SampleDensity(PlanetLocalPos);
		if (D <= 0.0f)
		{
			return FVoxelCell::Air();
		}
		FVoxelCell Cell = FVoxelCell::Solid(SampleMaterial(PlanetLocalPos, D), D);

		const double R = FMath::Sqrt(
			static_cast<double>(PlanetLocalPos.X) * PlanetLocalPos.X
			+ static_cast<double>(PlanetLocalPos.Y) * PlanetLocalPos.Y
			+ static_cast<double>(PlanetLocalPos.Z) * PlanetLocalPos.Z);
		if (R > 1e-6)
		{
			const FVector UnitDir(
				static_cast<float>(PlanetLocalPos.X / R),
				static_cast<float>(PlanetLocalPos.Y / R),
				static_cast<float>(PlanetLocalPos.Z / R));
			if (SampleScarCarveMeters(UnitDir) > 1.0f)
			{
				Cell.Flags |= static_cast<int32>(EVoxelFlags::Scarred);
			}
			const int32 Mat = Cell.MaterialId;
			if (Mat == static_cast<int32>(EVoxelMaterialId::OreIron)
				|| Mat == static_cast<int32>(EVoxelMaterialId::OreCopper)
				|| Mat == static_cast<int32>(EVoxelMaterialId::OreCrystal))
			{
				Cell.Flags |= static_cast<int32>(EVoxelFlags::OreVein);
			}
		}
		return Cell;
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
