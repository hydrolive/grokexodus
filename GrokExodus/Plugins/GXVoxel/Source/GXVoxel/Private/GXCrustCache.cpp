// Copyright Grok Exodus. All Rights Reserved.

#include "GXCrustCache.h"
#include "GXVoxelStamps.h"
#include "GXVoxel.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static uint64 MixU64(uint64 H, uint32 V)
{
	H ^= static_cast<uint64>(V) + 0x9E3779B97F4A7C15ull + (H << 6) + (H >> 2);
	return H;
}

static uint64 MixF(uint64 H, float F)
{
	uint32 U = 0;
	FMemory::Memcpy(&U, &F, 4);
	return MixU64(H, U);
}

uint64 FGXPlanetStampParams::Fingerprint() const
{
	uint64 H = MixU64(0xC04157ULL, Seed);
	H = MixU64(H, static_cast<uint32>(Profile));
	H = MixF(H, Radius);
	H = MixF(H, MaxRelief);
	H = MixF(H, CrustDepth);
	H = MixF(H, VoxelSize);
	H = MixF(H, ContinentFreq);
	H = MixF(H, MountainFreq);
	H = MixF(H, DetailFreq);
	H = MixF(H, SeaLevelBias);
	H = MixF(H, MoistureFreq);
	H = MixF(H, OreFreq);
	H = MixF(H, ScarFreq);
	H = MixF(H, ScarMaxDepth);
	H = MixF(H, ScarThreshold);
	H = MixF(H, OreThreshold);
	H = MixF(H, PlateFreq);
	H = MixF(H, HillFreq);
	H = MixF(H, RiverFreq);
	H = MixF(H, CanyonFreq);
	H = MixF(H, PlateauFreq);
	H = MixF(H, LocalRidgeFreq);
	H = MixF(H, LocalGullyFreq);
	H = MixF(H, VolcanoFreq);
	H = MixF(H, ValleyAmp);
	H = MixF(H, CanyonAmp);
	H = MixF(H, OceanDepthFrac);
	H = MixF(H, TrenchAmp);
	return H;
}

FString FGXCrustCache::FingerprintHex(const FGXPlanetStampParams& Params)
{
	return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Params.Fingerprint()));
}

FString FGXCrustCache::CacheDir(const FGXPlanetStampParams& Params)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelWorld"),
		FString::Printf(TEXT("crust_%s"), *FingerprintHex(Params)));
}

FString FGXCrustCache::AtlasPath(const FGXPlanetStampParams& Params)
{
	return FPaths::Combine(CacheDir(Params), TEXT("atlas.gxl1"));
}

FString FGXCrustCache::ChunkPath(const FGXPlanetStampParams& Params, const FGXChunkKey& Coord)
{
	return FPaths::Combine(CacheDir(Params),
		FString::Printf(TEXT("%d_%d_%d.gxm"), Coord.X, Coord.Y, Coord.Z));
}

bool FGXCrustCache::SaveMesh(const FString& Path, int32 LOD, const FGXMeshBuffers& Mesh)
{
	TArray<uint8> Buf;
	auto W = [&](const void* D, int32 S)
	{
		const int32 Off = Buf.Num();
		Buf.AddUninitialized(S);
		FMemory::Memcpy(Buf.GetData() + Off, D, S);
	};
	const uint32 Mag = 0x314D5847; // GXM1
	const int32 Ver = 1;
	const int32 NV = Mesh.Positions.Num();
	const int32 NI = Mesh.Indices.Num();
	W(&Mag, 4); W(&Ver, 4); W(&LOD, 4); W(&NV, 4); W(&NI, 4);
	if (NV > 0)
	{
		W(Mesh.Positions.GetData(), NV * sizeof(FVector));
		if (Mesh.Normals.Num() == NV) { const int32 One = 1; W(&One, 4); W(Mesh.Normals.GetData(), NV * sizeof(FVector)); }
		else { const int32 Zero = 0; W(&Zero, 4); }
		if (Mesh.UV0.Num() == NV) { const int32 One = 1; W(&One, 4); W(Mesh.UV0.GetData(), NV * sizeof(FVector2D)); }
		else { const int32 Zero = 0; W(&Zero, 4); }
		if (Mesh.Colors.Num() == NV) { const int32 One = 1; W(&One, 4); W(Mesh.Colors.GetData(), NV * sizeof(FLinearColor)); }
		else { const int32 Zero = 0; W(&Zero, 4); }
		if (Mesh.MaterialIds.Num() == NV) { const int32 One = 1; W(&One, 4); W(Mesh.MaterialIds.GetData(), NV * sizeof(int32)); }
		else { const int32 Zero = 0; W(&Zero, 4); }
	}
	if (NI > 0)
	{
		W(Mesh.Indices.GetData(), NI * sizeof(int32));
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(Buf, *Path);
}

bool FGXCrustCache::SaveHollow(const FString& Path)
{
	FGXMeshBuffers Empty;
	return SaveMesh(Path, 0, Empty);
}

bool FGXCrustCache::IsHollow(const FString& Path)
{
	int32 LOD = 0;
	FGXMeshBuffers Mesh;
	return LoadMesh(Path, LOD, Mesh) && Mesh.IsEmpty();
}

bool FGXCrustCache::LoadMesh(const FString& Path, int32& OutLOD, FGXMeshBuffers& OutMesh)
{
	if (!IFileManager::Get().FileExists(*Path))
	{
		return false;
	}
	TArray<uint8> Buf;
	if (!FFileHelper::LoadFileToArray(Buf, *Path) || Buf.Num() < 20)
	{
		return false;
	}
	int32 Off = 0;
	auto R = [&](void* D, int32 S) -> bool
	{
		if (Off + S > Buf.Num()) return false;
		FMemory::Memcpy(D, Buf.GetData() + Off, S);
		Off += S;
		return true;
	};
	uint32 Mag = 0;
	int32 Ver = 0, NV = 0, NI = 0;
	if (!R(&Mag, 4) || !R(&Ver, 4) || !R(&OutLOD, 4) || !R(&NV, 4) || !R(&NI, 4))
	{
		return false;
	}
	if (Mag != 0x314D5847 || Ver != 1 || NV < 0 || NI < 0 || NV > 2000000 || NI > 6000000)
	{
		return false;
	}
	OutMesh.Reset();
	if (NV > 0)
	{
		OutMesh.Positions.SetNumUninitialized(NV);
		if (!R(OutMesh.Positions.GetData(), NV * sizeof(FVector))) return false;
		int32 Flag = 0;
		if (!R(&Flag, 4)) return false;
		if (Flag)
		{
			OutMesh.Normals.SetNumUninitialized(NV);
			if (!R(OutMesh.Normals.GetData(), NV * sizeof(FVector))) return false;
		}
		if (!R(&Flag, 4)) return false;
		if (Flag)
		{
			OutMesh.UV0.SetNumUninitialized(NV);
			if (!R(OutMesh.UV0.GetData(), NV * sizeof(FVector2D))) return false;
		}
		if (!R(&Flag, 4)) return false;
		if (Flag)
		{
			OutMesh.Colors.SetNumUninitialized(NV);
			if (!R(OutMesh.Colors.GetData(), NV * sizeof(FLinearColor))) return false;
		}
		if (!R(&Flag, 4)) return false;
		if (Flag)
		{
			OutMesh.MaterialIds.SetNumUninitialized(NV);
			if (!R(OutMesh.MaterialIds.GetData(), NV * sizeof(int32))) return false;
		}
	}
	if (NI > 0)
	{
		OutMesh.Indices.SetNumUninitialized(NI);
		if (!R(OutMesh.Indices.GetData(), NI * sizeof(int32))) return false;
	}
	return true;
}

void FGXCrustCache::InvalidateChunk(const FGXPlanetStampParams& Params, const FGXChunkKey& Coord)
{
	IFileManager::Get().Delete(*ChunkPath(Params, Coord), false, true, true);
}
