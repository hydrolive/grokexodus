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
		if (LoadJpg(Dir / FString::Printf(TEXT("%s_A.jpg"), GLayerNames[I]), false, AlbedoSlices[I], W, H))
		{
			AlbedoSizes[I] = FIntPoint(W, H);
			++Loaded;
		}
		W = H = 0;
		LoadJpg(Dir / FString::Printf(TEXT("%s_N.jpg"), GLayerNames[I]), true, NormalSlices[I], W, H);
		NormalSizes[I] = FIntPoint(W, H);
		W = H = 0;
		LoadJpg(Dir / FString::Printf(TEXT("%s_R.jpg"), GLayerNames[I]), false, RoughSlices[I], W, H);
		RoughSizes[I] = FIntPoint(W, H);
	}

	AlbedoArray = BuildArray(AlbedoSlices, AlbedoSizes, false, TEXT("GXAlbedoArray"));
	NormalArray = BuildArray(NormalSlices, NormalSizes, true, TEXT("GXNormalArray"));
	RoughArray = BuildArray(RoughSlices, RoughSizes, false, TEXT("GXRoughArray"));

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
	Mid = nullptr;
	bReady = false;
}

UMaterialInterface* FGXTerrainPBR::GetMaterial() const
{
	return Mid;
}

UTexture2D* FGXTerrainPBR::LoadJpg(const FString& Path, bool bNormal, TArray<uint8>& OutBGRA, int32& OutW, int32& OutH)
{
	TArray<uint8> FileData;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToArray(FileData, *Path) || FileData.Num() == 0)
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXTerrainPBR: missing %s"), *Path);
		return nullptr;
	}
	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrap.IsValid() || !Wrap->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		Wrap = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrap.IsValid() || !Wrap->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			return nullptr;
		}
	}
	TArray64<uint8> Raw;
	if (!Wrap->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() == 0)
	{
		return nullptr;
	}
	OutW = Wrap->GetWidth();
	OutH = Wrap->GetHeight();
	OutBGRA.SetNumUninitialized(static_cast<int32>(Raw.Num()));
	FMemory::Memcpy(OutBGRA.GetData(), Raw.GetData(), Raw.Num());
	return nullptr;
}

void FGXTerrainPBR::ResizeBGRA(const TArray<uint8>& Src, int32 SW, int32 SH, TArray<uint8>& Dst, int32 DW, int32 DH) const
{
	Dst.SetNumUninitialized(DW * DH * 4);
	if (SW <= 0 || SH <= 0 || Src.Num() < SW * SH * 4)
	{
		FMemory::Memzero(Dst.GetData(), Dst.Num());
		return;
	}
	for (int32 Y = 0; Y < DH; ++Y)
	{
		const int32 SY = FMath::Clamp(Y * SH / DH, 0, SH - 1);
		for (int32 X = 0; X < DW; ++X)
		{
			const int32 SX = FMath::Clamp(X * SW / DW, 0, SW - 1);
			const int32 SI = (SY * SW + SX) * 4;
			const int32 DI = (Y * DW + X) * 4;
			Dst[DI + 0] = Src[SI + 0];
			Dst[DI + 1] = Src[SI + 1];
			Dst[DI + 2] = Src[SI + 2];
			Dst[DI + 3] = Src[SI + 3];
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
	const int32 SliceBytes = GTargetSize * GTargetSize * 4;
	TArray<uint8> Resized;
	for (int32 I = 0; I < GArraySlices; ++I)
	{
		ResizeBGRA(Slices[I], Sizes[I].X, Sizes[I].Y, Resized, GTargetSize, GTargetSize);
		FMemory::Memcpy(Dest + I * SliceBytes, Resized.GetData(), SliceBytes);
	}
	PD->Mips[0].BulkData.Unlock();
	Arr->UpdateResource();
	return Arr;
}
