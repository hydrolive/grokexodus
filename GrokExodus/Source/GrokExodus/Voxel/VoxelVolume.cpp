// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelVolume.h"

FVoxelVolume::FVoxelVolume(const FVoxelPlanetParams& Params)
	: Mapping(Params)
{
}

float FVoxelVolume::SampleDensity(const FVector& PlanetLocalPos) const
{
	const FIntVector VC = Mapping.WorldToVoxel(PlanetLocalPos);
	const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VC);
	if (const FVoxelChunk* Chunk = FindChunk(CC))
	{
		const FVoxelLocalCoord LC = FVoxelSphereMapping::VoxelToLocal(VC);
		return Chunk->At(LC.X, LC.Y, LC.Z).Density;
	}
	return Mapping.SampleDensity(PlanetLocalPos);
}

FVoxelCell FVoxelVolume::SampleCell(const FVector& PlanetLocalPos) const
{
	const FIntVector VC = Mapping.WorldToVoxel(PlanetLocalPos);
	return SampleVoxel(VC);
}

FVoxelCell FVoxelVolume::SampleVoxel(const FIntVector& VoxelCoord) const
{
	const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VoxelCoord);
	if (const FVoxelChunk* Chunk = FindChunk(CC))
	{
		const FVoxelLocalCoord LC = FVoxelSphereMapping::VoxelToLocal(VoxelCoord);
		return Chunk->At(LC.X, LC.Y, LC.Z);
	}
	return Mapping.SampleCell(Mapping.VoxelToWorldCenter(VoxelCoord));
}

FVoxelChunk* FVoxelVolume::FindChunk(const FVoxelChunkCoord& Coord)
{
	if (TUniquePtr<FVoxelChunk>* Ptr = Chunks.Find(Coord))
	{
		return Ptr->Get();
	}
	return nullptr;
}

const FVoxelChunk* FVoxelVolume::FindChunk(const FVoxelChunkCoord& Coord) const
{
	if (const TUniquePtr<FVoxelChunk>* Ptr = Chunks.Find(Coord))
	{
		return Ptr->Get();
	}
	return nullptr;
}

void FVoxelVolume::FillChunkProcedural(FVoxelChunk& Chunk) const
{
	for (int32 Z = 0; Z < FVoxelChunk::Size; ++Z)
	{
		for (int32 Y = 0; Y < FVoxelChunk::Size; ++Y)
		{
			for (int32 X = 0; X < FVoxelChunk::Size; ++X)
			{
				const FIntVector VC = FVoxelSphereMapping::ChunkLocalToVoxel(Chunk.Coord, X, Y, Z);
				const FVector World = Mapping.VoxelToWorldCenter(VC);
				Chunk.At(X, Y, Z) = Mapping.SampleCell(World);
			}
		}
	}
	Chunk.bMeshDirty = true;
}

FVoxelChunk& FVoxelVolume::GetOrCreateChunk(const FVoxelChunkCoord& Coord)
{
	if (TUniquePtr<FVoxelChunk>* Existing = Chunks.Find(Coord))
	{
		return **Existing;
	}

	TUniquePtr<FVoxelChunk> NewChunk = MakeUnique<FVoxelChunk>(Coord, 0);
	FillChunkProcedural(*NewChunk);
	FVoxelChunk& Ref = *NewChunk;
	Chunks.Add(Coord, MoveTemp(NewChunk));
	return Ref;
}

int32 FVoxelVolume::UnloadUnusedChunks(const TSet<FVoxelChunkCoord>& Keep)
{
	TArray<FVoxelChunkCoord> ToRemove;
	for (const TPair<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		const FVoxelChunk& C = *Pair.Value;
		if (C.bDirty || C.bBunkerResident)
		{
			continue;
		}
		if (!Keep.Contains(Pair.Key))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FVoxelChunkCoord& Key : ToRemove)
	{
		Chunks.Remove(Key);
	}
	return ToRemove.Num();
}

int64 FVoxelVolume::GetAllocatedMemoryBytes() const
{
	int64 Total = 0;
	for (const TPair<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		Total += Pair.Value->GetPayloadBytes();
		Total += sizeof(FVoxelChunk);
	}
	return Total;
}

void FVoxelVolume::GetDirtyChunkCoords(TArray<FVoxelChunkCoord>& Out) const
{
	Out.Reset();
	for (const TPair<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		if (Pair.Value->bDirty)
		{
			Out.Add(Pair.Key);
		}
	}
}

void FVoxelVolume::SetVoxel(const FIntVector& VoxelCoord, const FVoxelCell& Cell)
{
	const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VoxelCoord);
	FVoxelChunk& Chunk = GetOrCreateChunk(CC);
	const FVoxelLocalCoord LC = FVoxelSphereMapping::VoxelToLocal(VoxelCoord);
	Chunk.At(LC.X, LC.Y, LC.Z) = Cell;
	Chunk.MarkDirty();
}

void FVoxelVolume::MarkChunkAndNeighborsDirty(const FVoxelChunkCoord& Coord, TSet<FVoxelChunkCoord>& DirtySet)
{
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const FVoxelChunkCoord N(Coord.X + DX, Coord.Y + DY, Coord.Z + DZ);
				DirtySet.Add(N);
				if (FVoxelChunk* Chunk = FindChunk(N))
				{
					Chunk->bMeshDirty = true;
				}
			}
		}
	}
}

FVoxelVolume::FBrushResult FVoxelVolume::ApplySphereBrush(
	const FVector& Center,
	float Radius,
	bool bDig,
	int32 PlaceMaterialId,
	const FVoxelToolModifiers& Tool,
	float Strength)
{
	FBrushResult Result;
	if (Radius <= 0.0f || Strength <= 0.0f)
	{
		return Result;
	}

	// Precision cascade: higher precision shrinks soft falloff (cleaner edges)
	const float EffectiveRadius = Radius / FMath::Max(Tool.PrecisionMul, 0.25f);
	const float R2 = EffectiveRadius * EffectiveRadius;
	const float VoxelSize = Mapping.GetParams().VoxelSize;
	const float InvVoxel = 1.0f / VoxelSize;

	const int32 MinX = FMath::FloorToInt((Center.X - EffectiveRadius) * InvVoxel) - 1;
	const int32 MaxX = FMath::CeilToInt((Center.X + EffectiveRadius) * InvVoxel) + 1;
	const int32 MinY = FMath::FloorToInt((Center.Y - EffectiveRadius) * InvVoxel) - 1;
	const int32 MaxY = FMath::CeilToInt((Center.Y + EffectiveRadius) * InvVoxel) + 1;
	const int32 MinZ = FMath::FloorToInt((Center.Z - EffectiveRadius) * InvVoxel) - 1;
	const int32 MaxZ = FMath::CeilToInt((Center.Z + EffectiveRadius) * InvVoxel) + 1;

	TSet<FVoxelChunkCoord> DirtySet;
	TMap<int32, float> MaterialVolumes;
	const float CellVolume = VoxelSize * VoxelSize * VoxelSize;

	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FIntVector VC(X, Y, Z);
				const FVector World = Mapping.VoxelToWorldCenter(VC);
				const FVector Delta = World - Center;
				const float DistSq = Delta.SizeSquared();
				if (DistSq > R2)
				{
					continue;
				}

				const float Dist = FMath::Sqrt(DistSq);
				// Soft brush falloff [0,1]
				const float Falloff = 1.0f - (Dist / EffectiveRadius);
				const float Amount = Falloff * Strength;

				const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VC);
				FVoxelChunk& Chunk = GetOrCreateChunk(CC);
				const FVoxelLocalCoord LC = FVoxelSphereMapping::VoxelToLocal(VC);
				FVoxelCell& Cell = Chunk.At(LC.X, LC.Y, LC.Z);

				if (bDig)
				{
					if (!Cell.IsSolid())
					{
						continue;
					}

					const float Hardness = Materials.GetHardness(Cell.MaterialId);
					const float DigRate = FVoxelMaterialTable::ComputeDigRate(Hardness, Tool);
					const float DeltaDensity = Amount * DigRate * 2.0f; // scale: ~full carve at center with rate 1

					const float Before = Cell.Density;
					const int32 Mat = Cell.MaterialId;
					Cell.Density = Before - DeltaDensity;
					Cell.Flags |= EVoxelFlags::Deformed;

					if (Cell.Density <= 0.0f)
					{
						const float RemovedFrac = FMath::Clamp(Before / FMath::Max(Before + DeltaDensity, 0.001f), 0.0f, 1.0f);
						Result.VolumeChanged += CellVolume * RemovedFrac;
						MaterialVolumes.FindOrAdd(Mat) += CellVolume * RemovedFrac;
						Cell.Density = -1.0f;
						Cell.MaterialId = FVoxelConstants::MaterialAir;
					}
					else
					{
						const float RemovedFrac = FMath::Clamp(DeltaDensity / FMath::Max(Before, 0.001f), 0.0f, 1.0f);
						Result.VolumeChanged += CellVolume * RemovedFrac;
						MaterialVolumes.FindOrAdd(Mat) += CellVolume * RemovedFrac;
					}
				}
				else
				{
					// Place / fill
					const float Add = Amount * 2.0f;
					if (!Cell.IsSolid())
					{
						Cell.MaterialId = PlaceMaterialId;
						Cell.Density = Add;
						Cell.Flags |= EVoxelFlags::PlayerPlaced | EVoxelFlags::Deformed;
						Result.VolumeChanged += CellVolume * FMath::Min(Add, 1.0f);
						MaterialVolumes.FindOrAdd(PlaceMaterialId) += CellVolume * FMath::Min(Add, 1.0f);
					}
					else
					{
						Cell.Density = FMath::Min(Cell.Density + Add, 4.0f);
						Cell.MaterialId = PlaceMaterialId;
						Cell.Flags |= EVoxelFlags::PlayerPlaced | EVoxelFlags::Deformed;
						Result.VolumeChanged += CellVolume * FMath::Min(Add, 1.0f) * 0.25f;
					}
				}

				Chunk.MarkDirty();
				MarkChunkAndNeighborsDirty(CC, DirtySet);
			}
		}
	}

	// Dominant material by volume
	float BestVol = 0.0f;
	for (const TPair<int32, float>& Pair : MaterialVolumes)
	{
		if (Pair.Value > BestVol)
		{
			BestVol = Pair.Value;
			Result.DominantMaterialId = Pair.Key;
		}
	}

	Result.DirtyChunks = DirtySet.Array();
	return Result;
}

void FVoxelVolume::RegisterBunkerVolume(const FBox& PlanetLocalBounds)
{
	const FIntVector MinV = Mapping.WorldToVoxel(PlanetLocalBounds.Min);
	const FIntVector MaxV = Mapping.WorldToVoxel(PlanetLocalBounds.Max);

	const FVoxelChunkCoord MinC = FVoxelSphereMapping::VoxelToChunk(MinV);
	const FVoxelChunkCoord MaxC = FVoxelSphereMapping::VoxelToChunk(MaxV);

	for (int32 Z = MinC.Z; Z <= MaxC.Z; ++Z)
	{
		for (int32 Y = MinC.Y; Y <= MaxC.Y; ++Y)
		{
			for (int32 X = MinC.X; X <= MaxC.X; ++X)
			{
				FVoxelChunk& Chunk = GetOrCreateChunk(FVoxelChunkCoord(X, Y, Z));
				Chunk.bBunkerResident = true;
				// Flag cells inside bounds as bunker-protected
				for (int32 LZ = 0; LZ < FVoxelChunk::Size; ++LZ)
				{
					for (int32 LY = 0; LY < FVoxelChunk::Size; ++LY)
					{
						for (int32 LX = 0; LX < FVoxelChunk::Size; ++LX)
						{
							const FIntVector VC = FVoxelSphereMapping::ChunkLocalToVoxel(Chunk.Coord, LX, LY, LZ);
							const FVector W = Mapping.VoxelToWorldCenter(VC);
							if (PlanetLocalBounds.IsInsideOrOn(W))
							{
								Chunk.At(LX, LY, LZ).Flags |= static_cast<int32>(EVoxelFlags::BunkerProtected);
							}
						}
					}
				}
			}
		}
	}
}

void FVoxelVolume::ClearBunkerFlags()
{
	for (TPair<FVoxelChunkCoord, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		Pair.Value->bBunkerResident = false;
		for (FVoxelCell& Cell : Pair.Value->GetCellsMutable())
		{
			Cell.Flags &= ~static_cast<int32>(EVoxelFlags::BunkerProtected);
		}
	}
}
