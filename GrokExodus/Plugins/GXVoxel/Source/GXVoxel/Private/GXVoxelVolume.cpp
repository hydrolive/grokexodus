// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelVolume.h"

namespace GXVoxelPrivate
{
	static int32 DivFloor(int32 V, int32 Shift, int32 Mask)
	{
		if (V >= 0)
		{
			return V >> Shift;
		}
		return -((-V + Mask) >> Shift);
	}

	static int32 PositiveMod(int32 V, int32 Size)
	{
		const int32 M = V % Size;
		return M < 0 ? M + Size : M;
	}
}

FGXVoxelVolume::FGXVoxelVolume(const FGXPlanetStampParams& Params)
	: Stamp(Params)
{
	Generation.Value = 1;
}

FIntVector FGXVoxelVolume::WorldToVoxel(const FVector3d& PlanetLocalM, float VoxelSize)
{
	const double Inv = 1.0 / static_cast<double>(VoxelSize);
	return FIntVector(
		FMath::FloorToInt(PlanetLocalM.X * Inv),
		FMath::FloorToInt(PlanetLocalM.Y * Inv),
		FMath::FloorToInt(PlanetLocalM.Z * Inv));
}

FGXChunkKey FGXVoxelVolume::VoxelToChunk(const FIntVector& V)
{
	using namespace GXVoxelPrivate;
	return FGXChunkKey(
		DivFloor(V.X, FGXVoxelConstants::ChunkShift, FGXVoxelConstants::ChunkSize - 1),
		DivFloor(V.Y, FGXVoxelConstants::ChunkShift, FGXVoxelConstants::ChunkSize - 1),
		DivFloor(V.Z, FGXVoxelConstants::ChunkShift, FGXVoxelConstants::ChunkSize - 1));
}

void FGXVoxelVolume::VoxelToPage(const FIntVector& V, FGXChunkKey& OutChunk, FGXPageKey& OutPage, FIntVector& OutLocal)
{
	using namespace GXVoxelPrivate;
	OutChunk = VoxelToChunk(V);
	const int32 LX = PositiveMod(V.X, FGXVoxelConstants::ChunkSize);
	const int32 LY = PositiveMod(V.Y, FGXVoxelConstants::ChunkSize);
	const int32 LZ = PositiveMod(V.Z, FGXVoxelConstants::ChunkSize);
	OutPage.X = LX >> FGXVoxelConstants::PageShift;
	OutPage.Y = LY >> FGXVoxelConstants::PageShift;
	OutPage.Z = LZ >> FGXVoxelConstants::PageShift;
	OutLocal.X = LX & (FGXVoxelConstants::PageSize - 1);
	OutLocal.Y = LY & (FGXVoxelConstants::PageSize - 1);
	OutLocal.Z = LZ & (FGXVoxelConstants::PageSize - 1);
}

FGXVoxelPacked FGXVoxelVolume::Sample(const FVector3d& PlanetLocalM) const
{
	const float VoxelSize = Stamp.GetParams().VoxelSize;
	const FIntVector V = WorldToVoxel(PlanetLocalM, VoxelSize);
	FGXChunkKey Chunk;
	FGXPageKey Page;
	FIntVector Local;
	VoxelToPage(V, Chunk, Page, Local);

	if (const TArray<TSharedPtr<FGXVoxelPage, ESPMode::ThreadSafe>>* Slot = Pages.Find(Chunk))
	{
		const int32 PageIndex = Page.Index();
		if (Slot->IsValidIndex(PageIndex) && (*Slot)[PageIndex].IsValid())
		{
			const FGXVoxelPacked Stored = (*Slot)[PageIndex]->Get(Local.X, Local.Y, Local.Z);
			// Early sessions allocated pages of Density=0 / no flags. Those
			// look like grass (mesher used the stamp) but dig saw air and no-op'd.
			if (Stored.IsAuthoritative())
			{
				return Stored;
			}
		}
	}
	return Stamp.SamplePacked(PlanetLocalM);
}

FGXGenerationStamp FGXVoxelVolume::SetVoxel(const FIntVector& VoxelCoord, const FGXVoxelPacked& Cell)
{
	FGXChunkKey Chunk;
	FGXPageKey Page;
	FIntVector Local;
	VoxelToPage(VoxelCoord, Chunk, Page, Local);

	TArray<TSharedPtr<FGXVoxelPage, ESPMode::ThreadSafe>>& Slot = Pages.FindOrAdd(Chunk);
	if (Slot.Num() != FGXVoxelConstants::PagesPerChunk)
	{
		Slot.SetNum(FGXVoxelConstants::PagesPerChunk);
	}

	const int32 PageIndex = Page.Index();
	if (!Slot[PageIndex].IsValid())
	{
		Slot[PageIndex] = MakeShared<FGXVoxelPage, ESPMode::ThreadSafe>();
		// Fill from procedural so neighbors of the edit stay correct.
		const float VoxelSize = Stamp.GetParams().VoxelSize;
		for (int32 Z = 0; Z < FGXVoxelConstants::PageSize; ++Z)
		{
			for (int32 Y = 0; Y < FGXVoxelConstants::PageSize; ++Y)
			{
				for (int32 X = 0; X < FGXVoxelConstants::PageSize; ++X)
				{
					const FIntVector GV(
						Chunk.X * FGXVoxelConstants::ChunkSize + Page.X * FGXVoxelConstants::PageSize + X,
						Chunk.Y * FGXVoxelConstants::ChunkSize + Page.Y * FGXVoxelConstants::PageSize + Y,
						Chunk.Z * FGXVoxelConstants::ChunkSize + Page.Z * FGXVoxelConstants::PageSize + Z);
					const FVector3d Corner(
						static_cast<double>(GV.X) * VoxelSize,
						static_cast<double>(GV.Y) * VoxelSize,
						static_cast<double>(GV.Z) * VoxelSize);
					Slot[PageIndex]->Set(X, Y, Z, Stamp.SamplePacked(Corner));
				}
			}
		}
	}
	else if (Slot[PageIndex].GetSharedReferenceCount() > 1)
	{
		// Copy-on-write if a snapshot still holds this page.
		TSharedRef<FGXVoxelPage, ESPMode::ThreadSafe> Copy = MakeShared<FGXVoxelPage, ESPMode::ThreadSafe>(*Slot[PageIndex]);
		Slot[PageIndex] = Copy;
	}

	Slot[PageIndex]->Set(Local.X, Local.Y, Local.Z, Cell);
	++Generation.Value;
	if (Generation.Value == 0)
	{
		Generation.Value = 1;
	}
	return Generation;
}

void FGXVoxelVolume::GetAllocatedChunkKeys(TArray<FGXChunkKey>& Out) const
{
	Out.Reset();
	Pages.GetKeys(Out);
}

FGXVoxelVolume::FBrushResult FGXVoxelVolume::ApplySphereBrush(
	const FVector3d& CenterM,
	float RadiusM,
	bool bDig,
	uint8 PlaceMaterial,
	float Strength)
{
	FBrushResult Result;
	if (RadiusM <= 0.0f || Strength <= 0.0f)
	{
		return Result;
	}

	const float VoxelSize = Stamp.GetParams().VoxelSize;
	const float Inv = 1.0f / VoxelSize;
	const int32 MinX = FMath::FloorToInt((CenterM.X - RadiusM) * Inv) - 1;
	const int32 MaxX = FMath::CeilToInt((CenterM.X + RadiusM) * Inv) + 1;
	const int32 MinY = FMath::FloorToInt((CenterM.Y - RadiusM) * Inv) - 1;
	const int32 MaxY = FMath::CeilToInt((CenterM.Y + RadiusM) * Inv) + 1;
	const int32 MinZ = FMath::FloorToInt((CenterM.Z - RadiusM) * Inv) - 1;
	const int32 MaxZ = FMath::CeilToInt((CenterM.Z + RadiusM) * Inv) + 1;

	TSet<FGXChunkKey> Dirty;
	TMap<int32, float> Volumes;
	const float CellVol = VoxelSize * VoxelSize * VoxelSize;
	const float R2 = RadiusM * RadiusM;

	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FIntVector VC(X, Y, Z);
				// Same sample point the mesher uses (voxel corners, not cell centers).
				const FVector3d World(
					static_cast<double>(X) * VoxelSize,
					static_cast<double>(Y) * VoxelSize,
					static_cast<double>(Z) * VoxelSize);
				const FVector3d Delta = World - CenterM;
				const float D2 = static_cast<float>(Delta.SizeSquared());
				if (D2 > R2)
				{
					continue;
				}

				const float Falloff = 1.0f - FMath::Sqrt(D2 / R2);
				const float Amount = Falloff * Strength * VoxelSize;

				FGXVoxelPacked Cell = Sample(World);
				const float OldD = Cell.ToDensityMeters();
				float NewD = OldD;
				if (bDig)
				{
					if (OldD <= 0.0f)
					{
						continue;
					}
					NewD = OldD - Amount;
					if (NewD > 0.0f)
					{
						Cell = FGXVoxelPacked::FromDensity(NewD, Cell.Material, Cell.Flags | EGXVoxelFlags::Deformed);
					}
					else
					{
						Cell = FGXVoxelPacked::MakeAir();
						Cell.Flags = EGXVoxelFlags::Deformed;
					}
					const float Removed = FMath::Max(0.0f, OldD - FMath::Max(NewD, 0.0f));
					Result.VolumeChanged += Removed * CellVol;
					Volumes.FindOrAdd(Cell.Material > 0 ? Cell.Material : 1) += Removed * CellVol;
				}
				else
				{
					NewD = FMath::Min(FGXVoxelPacked::MaxAbsDensityM, OldD + Amount);
					Cell = FGXVoxelPacked::FromDensity(NewD, PlaceMaterial, EGXVoxelFlags::PlayerPlaced | EGXVoxelFlags::Deformed);
					Result.VolumeChanged += FMath::Max(0.0f, NewD - OldD) * CellVol;
					Volumes.FindOrAdd(PlaceMaterial) += FMath::Max(0.0f, NewD - OldD) * CellVol;
				}

				SetVoxel(VC, Cell);
				Dirty.Add(VoxelToChunk(VC));
				// MC pads one voxel: remesh the face-neighbor only if we edited the seam.
				constexpr int32 CS = FGXVoxelConstants::ChunkSize;
				const int32 LX = GXVoxelPrivate::PositiveMod(VC.X, CS);
				const int32 LY = GXVoxelPrivate::PositiveMod(VC.Y, CS);
				const int32 LZ = GXVoxelPrivate::PositiveMod(VC.Z, CS);
				if (LX == 0) Dirty.Add(VoxelToChunk(FIntVector(VC.X - 1, VC.Y, VC.Z)));
				if (LX == CS - 1) Dirty.Add(VoxelToChunk(FIntVector(VC.X + 1, VC.Y, VC.Z)));
				if (LY == 0) Dirty.Add(VoxelToChunk(FIntVector(VC.X, VC.Y - 1, VC.Z)));
				if (LY == CS - 1) Dirty.Add(VoxelToChunk(FIntVector(VC.X, VC.Y + 1, VC.Z)));
				if (LZ == 0) Dirty.Add(VoxelToChunk(FIntVector(VC.X, VC.Y, VC.Z - 1)));
				if (LZ == CS - 1) Dirty.Add(VoxelToChunk(FIntVector(VC.X, VC.Y, VC.Z + 1)));
			}
		}
	}

	float Best = 0.0f;
	for (const auto& Pair : Volumes)
	{
		if (Pair.Value > Best)
		{
			Best = Pair.Value;
			Result.DominantMaterialId = Pair.Key;
		}
	}
	Result.DirtyChunks = Dirty.Array();
	return Result;
}

int32 FGXVoxelVolume::GetAllocatedPageCount() const
{
	int32 N = 0;
	for (const auto& Pair : Pages)
	{
		for (const TSharedPtr<FGXVoxelPage, ESPMode::ThreadSafe>& P : Pair.Value)
		{
			if (P.IsValid())
			{
				++N;
			}
		}
	}
	return N;
}

int64 FGXVoxelVolume::GetAllocatedBytes() const
{
	return static_cast<int64>(GetAllocatedPageCount()) * static_cast<int64>(sizeof(FGXVoxelPage));
}

TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> FGXVoxelVolume::PublishSnapshot() const
{
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = MakeShared<FGXVoxelSnapshot, ESPMode::ThreadSafe>();
	Snap->Stamp = Generation;
	Snap->Params = Stamp.GetParams();
	for (const auto& Pair : Pages)
	{
		TArray<TSharedPtr<const FGXVoxelPage, ESPMode::ThreadSafe>> ConstPages;
		ConstPages.Reserve(Pair.Value.Num());
		for (const TSharedPtr<FGXVoxelPage, ESPMode::ThreadSafe>& P : Pair.Value)
		{
			ConstPages.Add(P);
		}
		Snap->Pages.Add(Pair.Key, MoveTemp(ConstPages));
	}
	return Snap;
}

FGXVoxelPacked FGXVoxelSnapshot::Sample(const FVector3d& PlanetLocalM) const
{
	const FIntVector V = FGXVoxelVolume::WorldToVoxel(PlanetLocalM, Params.VoxelSize);
	FGXChunkKey Chunk;
	FGXPageKey Page;
	FIntVector Local;
	FGXVoxelVolume::VoxelToPage(V, Chunk, Page, Local);
	if (const TArray<TSharedPtr<const FGXVoxelPage, ESPMode::ThreadSafe>>* Slot = Pages.Find(Chunk))
	{
		const int32 PageIndex = Page.Index();
		if (Slot->IsValidIndex(PageIndex) && (*Slot)[PageIndex].IsValid())
		{
			const FGXVoxelPacked Stored = (*Slot)[PageIndex]->Get(Local.X, Local.Y, Local.Z);
			if (Stored.IsAuthoritative())
			{
				return Stored;
			}
		}
	}
	FGXSphereStamp Eval(Params);
	return Eval.SamplePacked(PlanetLocalM);
}

bool FGXVoxelSnapshot::HasStored(const FVector3d& PlanetLocalM) const
{
	const FIntVector V = FGXVoxelVolume::WorldToVoxel(PlanetLocalM, Params.VoxelSize);
	FGXChunkKey Chunk;
	FGXPageKey Page;
	FIntVector Local;
	FGXVoxelVolume::VoxelToPage(V, Chunk, Page, Local);
	if (const TArray<TSharedPtr<const FGXVoxelPage, ESPMode::ThreadSafe>>* Slot = Pages.Find(Chunk))
	{
		const int32 PageIndex = Page.Index();
		return Slot->IsValidIndex(PageIndex) && (*Slot)[PageIndex].IsValid();
	}
	return false;
}
