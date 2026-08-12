// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelPersistence.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

namespace VoxelPersistPrivate
{
	struct FHeader
	{
		uint32 Magic = 0;
		uint32 Version = 0;
		uint32 Seed = 0;
		float Radius = 0;
		float MaxRelief = 0;
		float VoxelSize = 0;
		int32 ChunkSize = 0;
		int32 DirtyChunkCount = 0;
	};

	template<typename T>
	void WritePod(TArray<uint8>& Buf, const T& Value)
	{
		const int32 Offset = Buf.Num();
		Buf.AddUninitialized(sizeof(T));
		FMemory::Memcpy(Buf.GetData() + Offset, &Value, sizeof(T));
	}

	template<typename T>
	bool ReadPod(const TArray<uint8>& Buf, int32& Offset, T& Out)
	{
		if (Offset + static_cast<int32>(sizeof(T)) > Buf.Num())
		{
			return false;
		}
		FMemory::Memcpy(&Out, Buf.GetData() + Offset, sizeof(T));
		Offset += sizeof(T);
		return true;
	}
}

bool FVoxelPersistence::SaveToBuffer(const FVoxelVolume& Volume, TArray<uint8>& OutBuffer)
{
	using namespace VoxelPersistPrivate;

	OutBuffer.Reset();

	TArray<FVoxelChunkCoord> Dirty;
	Volume.GetDirtyChunkCoords(Dirty);

	const FVoxelPlanetParams& P = Volume.GetPlanetParams();

	FHeader Header;
	Header.Magic = FVoxelConstants::PersistenceMagic;
	Header.Version = FVoxelConstants::PersistenceVersion;
	Header.Seed = P.Seed;
	Header.Radius = P.Radius;
	Header.MaxRelief = P.MaxRelief;
	Header.VoxelSize = P.VoxelSize;
	Header.ChunkSize = FVoxelConstants::ChunkSize;
	Header.DirtyChunkCount = Dirty.Num();
	WritePod(OutBuffer, Header);

	for (const FVoxelChunkCoord& Coord : Dirty)
	{
		const FVoxelChunk* Chunk = Volume.FindChunk(Coord);
		if (!Chunk)
		{
			continue;
		}

		WritePod(OutBuffer, Coord.X);
		WritePod(OutBuffer, Coord.Y);
		WritePod(OutBuffer, Coord.Z);

		const uint8 Bunker = Chunk->bBunkerResident ? 1 : 0;
		WritePod(OutBuffer, Bunker);
		const uint8 Pad[3] = { 0, 0, 0 };
		WritePod(OutBuffer, Pad[0]);
		WritePod(OutBuffer, Pad[1]);
		WritePod(OutBuffer, Pad[2]);

		const TArray<FVoxelCell>& Cells = Chunk->GetCells();
		const int32 Bytes = Cells.Num() * static_cast<int32>(sizeof(FVoxelCell));
		const int32 Offset = OutBuffer.Num();
		OutBuffer.AddUninitialized(Bytes);
		FMemory::Memcpy(OutBuffer.GetData() + Offset, Cells.GetData(), Bytes);
	}

	return true;
}

bool FVoxelPersistence::LoadFromBuffer(FVoxelVolume& Volume, const TArray<uint8>& Buffer)
{
	using namespace VoxelPersistPrivate;

	int32 Offset = 0;
	FHeader Header;
	if (!ReadPod(Buffer, Offset, Header))
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: buffer too small for header"));
		return false;
	}

	if (Header.Magic != FVoxelConstants::PersistenceMagic)
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: bad magic 0x%08X"), Header.Magic);
		return false;
	}
	if (Header.Version != FVoxelConstants::PersistenceVersion)
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: unsupported version %u"), Header.Version);
		return false;
	}
	if (Header.ChunkSize != FVoxelConstants::ChunkSize)
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: chunk size mismatch %d"), Header.ChunkSize);
		return false;
	}

	// Align planet params with saved seed/radius (keep other noise knobs from current volume if desired)
	FVoxelPlanetParams Params = Volume.GetPlanetParams();
	Params.Seed = Header.Seed;
	Params.Radius = Header.Radius;
	Params.MaxRelief = Header.MaxRelief;
	Params.VoxelSize = Header.VoxelSize;
	Volume.GetMappingMutable().SetParams(Params);

	for (int32 I = 0; I < Header.DirtyChunkCount; ++I)
	{
		int32 CX = 0, CY = 0, CZ = 0;
		uint8 Bunker = 0, P0 = 0, P1 = 0, P2 = 0;
		if (!ReadPod(Buffer, Offset, CX) || !ReadPod(Buffer, Offset, CY) || !ReadPod(Buffer, Offset, CZ)
			|| !ReadPod(Buffer, Offset, Bunker) || !ReadPod(Buffer, Offset, P0)
			|| !ReadPod(Buffer, Offset, P1) || !ReadPod(Buffer, Offset, P2))
		{
			UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: truncated chunk header at %d"), I);
			return false;
		}

		const int32 CellBytes = FVoxelChunk::CellCount * static_cast<int32>(sizeof(FVoxelCell));
		if (Offset + CellBytes > Buffer.Num())
		{
			UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: truncated cell payload at %d"), I);
			return false;
		}

		const FVoxelChunkCoord Coord(CX, CY, CZ);
		FVoxelChunk& Chunk = Volume.GetOrCreateChunk(Coord);
		FMemory::Memcpy(Chunk.GetCellsMutable().GetData(), Buffer.GetData() + Offset, CellBytes);
		Offset += CellBytes;

		Chunk.bDirty = true;
		Chunk.bMeshDirty = true;
		Chunk.bBunkerResident = Bunker != 0;
	}

	return true;
}

bool FVoxelPersistence::SaveToFile(const FVoxelVolume& Volume, const FString& FilePath)
{
	TArray<uint8> Buffer;
	if (!SaveToBuffer(Volume, Buffer))
	{
		return false;
	}

	// Ensure directory exists
	const FString Dir = FPaths::GetPath(FilePath);
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!Dir.IsEmpty() && !PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}

	if (!FFileHelper::SaveArrayToFile(Buffer, *FilePath))
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: failed to write %s"), *FilePath);
		return false;
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelPersistence: saved %d bytes to %s"), Buffer.Num(), *FilePath);
	return true;
}

bool FVoxelPersistence::LoadFromFile(FVoxelVolume& Volume, const FString& FilePath)
{
	TArray<uint8> Buffer;
	if (!FFileHelper::LoadFileToArray(Buffer, *FilePath))
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelPersistence: failed to read %s"), *FilePath);
		return false;
	}
	return LoadFromBuffer(Volume, Buffer);
}

bool FVoxelPersistence::BuffersEqual(const TArray<uint8>& A, const TArray<uint8>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	return FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0;
}
