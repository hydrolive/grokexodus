// Copyright Grok Exodus. All Rights Reserved.

#include "GXTerrainPBR.h"
#include "GXVoxel.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureDefines.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PixelFormat.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 GArraySlices = 8;
	constexpr int32 GTargetSize = 512;

	const TCHAR* GLayerNames[GArraySlices] = {
		TEXT("T_TemperateGrass"), // 0 unused / fallback
		TEXT("T_TemperateGrass"), // 1
		TEXT("T_RockyCliff"),     // 2
		TEXT("T_DryDirt"),        // 3
		TEXT("T_SandCoastal"),    // 4
		TEXT("T_SnowIce"),        // 5
		TEXT("T_WetMud"),         // 6
		TEXT("T_VolcanicScorched")// 7
	};
}

FString FGXTerrainPBR::GetSourceDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Voxel/Textures/Source"));
}

void FGXTerrainPBR::Initialize(UObject* Outer)
{
	if (bReady)
	{
		return;
	}
	OuterPtr = Outer;

	const FString Dir = GetSourceDir();
	TArray<TArray<uint8>> AlbedoSlices, NormalSlices, RoughSlices;
	TArray<FIntPoint> AlbedoSizes, NormalSizes, RoughSizes;
	AlbedoSlices.SetNum(GArraySlices);
	NormalSlices.SetNum(GArraySlices);
	RoughSlices.SetNum(GArraySlices);
	AlbedoSizes.SetNum(GArraySlices);
	NormalSizes.SetNum(GArraySlices);
	RoughSizes.SetNum(GArraySlices);

	int32 Loaded = 0;
	for (int32 I = 0; I < GArraySlices; ++I)
	{
		int32 W = 0, H = 0;
		if (LoadJpg(Dir / FString::Printf(TEXT("%s_A.jpg"), GLayerNames[I]), AlbedoSlices[I], W, H))
		{
			AlbedoSizes[I] = FIntPoint(W, H);
			++Loaded;
		}
		W = H = 0;
		if (LoadJpg(Dir / FString::Printf(TEXT("%s_N.jpg"), GLayerNames[I]), NormalSlices[I], W, H))
		{
			NormalSizes[I] = FIntPoint(W, H);
		}
		W = H = 0;
		if (LoadJpg(Dir / FString::Printf(TEXT("%s_R.jpg"), GLayerNames[I]), RoughSlices[I], W, H))
		{
			RoughSizes[I] = FIntPoint(W, H);
		}
	}

	AlbedoArray = BuildArray(AlbedoSlices, AlbedoSizes, false, TEXT("GXAlbedoArray"));
	NormalArray = BuildArray(NormalSlices, NormalSizes, true, TEXT("GXNormalArray"));
	RoughArray = BuildArray(RoughSlices, RoughSizes, false, TEXT("GXRoughArray"));
	AlbedoAtlas = BuildAtlas(AlbedoSlices, AlbedoSizes, false, TEXT("GXAlbedoAtlas"));
	NormalAtlas = BuildAtlas(NormalSlices, NormalSizes, true, TEXT("GXNormalAtlas"));
	RoughAtlas = BuildAtlas(RoughSlices, RoughSizes, false, TEXT("GXRoughAtlas"));

	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Voxel/Materials/M_VoxelTerrain_PBR.M_VoxelTerrain_PBR"));
	if (Parent && Outer)
	{
		Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		if (Mid)
		{
			if (AlbedoArray) Mid->SetTextureParameterValue(TEXT("AlbedoArray"), AlbedoArray);
			if (NormalArray) Mid->SetTextureParameterValue(TEXT("NormalArray"), NormalArray);
			if (RoughArray) Mid->SetTextureParameterValue(TEXT("RoughArray"), RoughArray);
			if (AlbedoAtlas) Mid->SetTextureParameterValue(TEXT("AlbedoAtlas"), AlbedoAtlas);
			if (NormalAtlas) Mid->SetTextureParameterValue(TEXT("NormalAtlas"), NormalAtlas);
			if (RoughAtlas) Mid->SetTextureParameterValue(TEXT("RoughAtlas"), RoughAtlas);
			Mid->SetScalarParameterValue(TEXT("TileScale"), 0.0045f);
			Mid->SetScalarParameterValue(TEXT("SlopeStart"), 0.32f);
			Mid->SetScalarParameterValue(TEXT("SlopeEnd"), 0.72f);
			Mid->SetScalarParameterValue(TEXT("HeightSharpness"), 0.28f);
			Roots.Add(Mid);
		}
	}

	bReady = true;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXTerrainPBR: loaded %d/%d albedo sets, material=%s"),
		Loaded, GArraySlices, *GetNameSafe(Mid ? Mid : Parent));
}

void FGXTerrainPBR::Shutdown()
{
	for (UObject* Obj : Roots)
	{
		if (IsValid(Obj))
		{
			Obj->RemoveFromRoot();
		}
	}
	Roots.Reset();
	AlbedoArray = nullptr;
	NormalArray = nullptr;
	RoughArray = nullptr;
	AlbedoAtlas = nullptr;
	NormalAtlas = nullptr;
	RoughAtlas = nullptr;
	Mid = nullptr;
	bReady = false;
}

UMaterialInterface* FGXTerrainPBR::GetMaterial() const
{
	return Mid;
}

bool FGXTerrainPBR::LoadJpg(const FString& Path, TArray<uint8>& OutBGRA, int32& OutW, int32& OutH)
{
	OutW = 0;
	OutH = 0;
	OutBGRA.Reset();

	TArray<uint8> FileData;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToArray(FileData, *Path) || FileData.Num() == 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXTerrainPBR: missing %s"), *Path);
		return false;
	}
	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrap.IsValid() || !Wrap->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		Wrap = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrap.IsValid() || !Wrap->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			return false;
		}
	}
	TArray64<uint8> Raw;
	if (!Wrap->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() == 0)
	{
		return false;
	}
	const int32 W = Wrap->GetWidth();
	const int32 H = Wrap->GetHeight();
	const int64 Expected = static_cast<int64>(W) * static_cast<int64>(H) * 4;
	if (W <= 0 || H <= 0 || Expected <= 0 || Raw.Num() < Expected || Expected > MAX_int32)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXTerrainPBR: bad decode %s w=%d h=%d raw=%lld"),
			*Path, W, H, static_cast<int64>(Raw.Num()));
		return false;
	}
	OutBGRA.SetNumUninitialized(static_cast<int32>(Expected));
	FMemory::Memcpy(OutBGRA.GetData(), Raw.GetData(), static_cast<SIZE_T>(Expected));
	OutW = W;
	OutH = H;
	return true;
}

void FGXTerrainPBR::ResizeBGRA(const TArray<uint8>& Src, int32 SW, int32 SH, TArray<uint8>& Dst, int32 DW, int32 DH) const
{
	const int64 DestBytes = static_cast<int64>(DW) * static_cast<int64>(DH) * 4;
	const int64 SrcBytes = static_cast<int64>(SW) * static_cast<int64>(SH) * 4;
	if (DW <= 0 || DH <= 0 || DestBytes <= 0 || DestBytes > MAX_int32)
	{
		Dst.Reset();
		return;
	}
	Dst.SetNumZeroed(static_cast<int32>(DestBytes));
	if (SW <= 0 || SH <= 0 || SrcBytes <= 0 || Src.Num() < SrcBytes)
	{
		return;
	}

	uint8* DstData = Dst.GetData();
	const uint8* SrcData = Src.GetData();
	for (int32 Y = 0; Y < DH; ++Y)
	{
		const int32 SY = FMath::Clamp(Y * SH / DH, 0, SH - 1);
		for (int32 X = 0; X < DW; ++X)
		{
			const int32 SX = FMath::Clamp(X * SW / DW, 0, SW - 1);
			const int64 SI = (static_cast<int64>(SY) * SW + SX) * 4;
			const int64 DI = (static_cast<int64>(Y) * DW + X) * 4;
			if (SI + 3 >= Src.Num() || DI + 3 >= Dst.Num())
			{
				continue;
			}
			DstData[DI + 0] = SrcData[SI + 0];
			DstData[DI + 1] = SrcData[SI + 1];
			DstData[DI + 2] = SrcData[SI + 2];
			DstData[DI + 3] = SrcData[SI + 3];
		}
	}
}

UTexture2DArray* FGXTerrainPBR::BuildArray(const TArray<TArray<uint8>>& Slices, const TArray<FIntPoint>& Sizes, bool bNormal, const TCHAR* Name)
{
	UTexture2DArray* Arr = UTexture2DArray::CreateTransient(GTargetSize, GTargetSize, GArraySlices, PF_B8G8R8A8, Name);
	if (!Arr)
	{
		return nullptr;
	}
	Arr->SRGB = !bNormal;
	Arr->CompressionSettings = bNormal ? TC_Normalmap : TC_Default;
	Arr->Filter = TF_Bilinear;
	Arr->AddressX = TA_Wrap;
	Arr->AddressY = TA_Wrap;
	Arr->AddressZ = TA_Clamp;
	Arr->AddToRoot();
	Roots.Add(Arr);

	FTexturePlatformData* PD = Arr->GetPlatformData();
	if (!PD || PD->Mips.Num() == 0)
	{
		return Arr;
	}
	uint8* Dest = static_cast<uint8*>(PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	if (!Dest)
	{
		return Arr;
	}
	const int32 SliceBytes = GTargetSize * GTargetSize * 4;
	TArray<uint8> Resized;
	for (int32 I = 0; I < GArraySlices; ++I)
	{
		const FIntPoint Sz = Sizes.IsValidIndex(I) ? Sizes[I] : FIntPoint(0, 0);
		static const TArray<uint8> EmptySlice;
		const TArray<uint8>& Slice = Slices.IsValidIndex(I) ? Slices[I] : EmptySlice;
		ResizeBGRA(Slice, Sz.X, Sz.Y, Resized, GTargetSize, GTargetSize);
		if (Resized.Num() < SliceBytes)
		{
			Resized.SetNumZeroed(SliceBytes);
		}
		FMemory::Memcpy(Dest + static_cast<int64>(I) * SliceBytes, Resized.GetData(), SliceBytes);
	}
	PD->Mips[0].BulkData.Unlock();
	Arr->UpdateResource();
	return Arr;
}

UTexture2D* FGXTerrainPBR::BuildAtlas(const TArray<TArray<uint8>>& Slices, const TArray<FIntPoint>& Sizes, bool bNormal, const TCHAR* Name)
{
	constexpr int32 Cols = 4;
	constexpr int32 Rows = 2;
	const int32 AW = GTargetSize * Cols;
	const int32 AH = GTargetSize * Rows;
	UTexture2D* Atlas = UTexture2D::CreateTransient(AW, AH, PF_B8G8R8A8, Name);
	if (!Atlas)
	{
		return nullptr;
	}
	Atlas->SRGB = !bNormal;
	Atlas->CompressionSettings = bNormal ? TC_Normalmap : TC_Default;
	Atlas->Filter = TF_Bilinear;
	Atlas->AddressX = TA_Clamp;
	Atlas->AddressY = TA_Clamp;
	Atlas->AddToRoot();
	Roots.Add(Atlas);

	FTexturePlatformData* PD = Atlas->GetPlatformData();
	if (!PD || PD->Mips.Num() == 0)
	{
		return Atlas;
	}
	uint8* Dest = static_cast<uint8*>(PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	if (!Dest)
	{
		return Atlas;
	}
	FMemory::Memzero(Dest, static_cast<SIZE_T>(AW) * AH * 4);
	TArray<uint8> Cell;
	const int32 CellBytes = GTargetSize * GTargetSize * 4;
	for (int32 I = 0; I < GArraySlices; ++I)
	{
		const FIntPoint Sz = Sizes.IsValidIndex(I) ? Sizes[I] : FIntPoint(0, 0);
		static const TArray<uint8> EmptySlice;
		const TArray<uint8>& Slice = Slices.IsValidIndex(I) ? Slices[I] : EmptySlice;
		ResizeBGRA(Slice, Sz.X, Sz.Y, Cell, GTargetSize, GTargetSize);
		if (Cell.Num() < CellBytes)
		{
			Cell.SetNumZeroed(CellBytes);
		}
		const int32 Col = I % Cols;
		const int32 Row = I / Cols;
		const int32 X0 = Col * GTargetSize;
		const int32 Y0 = Row * GTargetSize;
		for (int32 Y = 0; Y < GTargetSize; ++Y)
		{
			FMemory::Memcpy(
				Dest + (static_cast<int64>(Y0 + Y) * AW + X0) * 4,
				Cell.GetData() + static_cast<int64>(Y) * GTargetSize * 4,
				GTargetSize * 4);
		}
	}
	PD->Mips[0].BulkData.Unlock();
	Atlas->UpdateResource();
	return Atlas;
}
