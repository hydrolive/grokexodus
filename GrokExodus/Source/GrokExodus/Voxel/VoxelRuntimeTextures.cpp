// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelRuntimeTextures.h"
#include "Voxel/VoxelTypes.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/Material.h"
#include "UObject/Package.h"

FString FVoxelRuntimeTextures::GetSourceTextureDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Voxel/Textures/Source"));
}

void FVoxelRuntimeTextures::Initialize(UObject* Outer)
{
	if (bLoaded)
	{
		return;
	}
	OuterPtr = Outer;

	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::TemperateGrass), TEXT("T_TemperateGrass"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::RockyCliff), TEXT("T_RockyCliff"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::DryDirt), TEXT("T_DryDirt"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::SandCoastal), TEXT("T_SandCoastal"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::SnowIce), TEXT("T_SnowIce"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::WetMud), TEXT("T_WetMud"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::VolcanicScorched), TEXT("T_VolcanicScorched"));
	LoadMaterialSet(static_cast<int32>(EVoxelMaterialId::BedrockDeep), TEXT("T_RockyCliff"));

	const FString MetalPath = GetSourceTextureDir() / TEXT("T_Shared_Metallic.jpg");
	SharedMetallic = LoadTextureFile(MetalPath, false, nullptr, nullptr, nullptr);

	// Count sets with valid CPU albedo data
	int32 ValidCpuSets = 0;
	for (const TPair<int32, FMaterialTextures>& Pair : ByMaterial)
	{
		if (Pair.Value.HasCpuAlbedo())
		{
			++ValidCpuSets;
		}
	}
	bHasAnyCpuAlbedo = ValidCpuSets > 0;

	EnsureMaterials(Outer);
	bLoaded = true;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelRuntimeTextures: loaded %d material sets (%d with CPU albedo) from %s"),
		ByMaterial.Num(), ValidCpuSets, *GetSourceTextureDir());
}

void FVoxelRuntimeTextures::LoadMaterialSet(int32 MaterialId, const TCHAR* BaseName)
{
	FMaterialTextures& Set = ByMaterial.FindOrAdd(MaterialId);
	const FString Dir = GetSourceTextureDir();

	Set.Albedo = LoadTextureFile(Dir / FString::Printf(TEXT("%s_A.jpg"), BaseName), false, &Set.AlbedoBGRA, &Set.AlbedoW, &Set.AlbedoH);
	Set.Normal = LoadTextureFile(Dir / FString::Printf(TEXT("%s_N.jpg"), BaseName), true, nullptr, nullptr, nullptr);
	Set.Roughness = LoadTextureFile(Dir / FString::Printf(TEXT("%s_R.jpg"), BaseName), false, nullptr, nullptr, nullptr);
	Set.Metallic = SharedMetallic;

	// Validate CPU buffer size matches dimensions
	const int64 ExpectedBytes = static_cast<int64>(Set.AlbedoW) * static_cast<int64>(Set.AlbedoH) * 4;
	if (Set.AlbedoW <= 0 || Set.AlbedoH <= 0 || Set.AlbedoBGRA.Num() < ExpectedBytes)
	{
		Set.AlbedoW = 0;
		Set.AlbedoH = 0;
		Set.AlbedoBGRA.Reset();
		return;
	}

	// Average color for distant / fallback
	double R = 0, G = 0, B = 0;
	const int64 N = static_cast<int64>(Set.AlbedoW) * Set.AlbedoH;
	const int64 Step = FMath::Max<int64>(1, N / 4096);
	int32 Count = 0;
	for (int64 I = 0; I < N; I += Step)
	{
		const int64 O = I * 4;
		if (O + 2 >= Set.AlbedoBGRA.Num())
		{
			break;
		}
		B += Set.AlbedoBGRA.GetData()[O + 0];
		G += Set.AlbedoBGRA.GetData()[O + 1];
		R += Set.AlbedoBGRA.GetData()[O + 2];
		++Count;
	}
	if (Count > 0)
	{
		Set.AverageColor = FLinearColor(
			static_cast<float>(R / Count) / 255.0f,
			static_cast<float>(G / Count) / 255.0f,
			static_cast<float>(B / Count) / 255.0f,
			1.0f);
	}
}

UTexture2D* FVoxelRuntimeTextures::LoadTextureFile(const FString& AbsolutePath, bool bNormalMap, TArray64<uint8>* OutBGRA, int32* OutW, int32* OutH)
{
	TArray<uint8> FileData;
	if (!FPaths::FileExists(AbsolutePath) || !FFileHelper::LoadFileToArray(FileData, *AbsolutePath) || FileData.Num() == 0)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelRuntimeTextures: missing %s"), *AbsolutePath);
		return nullptr;
	}

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ImageWrapper")))
	{
		FModuleManager::Get().LoadModule(TEXT("ImageWrapper"));
	}
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ImageWrapper")))
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelRuntimeTextures: ImageWrapper module not available"));
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelRuntimeTextures: decode failed %s"), *AbsolutePath);
			return nullptr;
		}
	}

	TArray64<uint8> RawBGRA;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA) || RawBGRA.Num() == 0)
	{
		return nullptr;
	}

	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	if (W <= 0 || H <= 0)
	{
		return nullptr;
	}

	const int64 Expected = static_cast<int64>(W) * static_cast<int64>(H) * 4;
	if (RawBGRA.Num() < Expected)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelRuntimeTextures: truncated image %s (%lld < %lld)"),
			*AbsolutePath, RawBGRA.Num(), Expected);
		return nullptr;
	}

	if (OutBGRA)
	{
		*OutBGRA = MoveTemp(RawBGRA);
	}
	if (OutW) *OutW = W;
	if (OutH) *OutH = H;

	// Optional GPU texture (rooted so GC won't collect during play)
	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->AddToRoot(); // prevent GC of transient textures
	RootedTextures.Add(Texture);

	Texture->SRGB = !bNormalMap;
	Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Default;
	Texture->AddressX = TA_Wrap;
	Texture->AddressY = TA_Wrap;
	Texture->Filter = TF_Bilinear;

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (PlatformData && PlatformData->Mips.Num() > 0)
	{
		void* MipData = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		if (MipData)
		{
			const TArray64<uint8>& Src = OutBGRA ? *OutBGRA : RawBGRA;
			const int64 CopyBytes = FMath::Min<int64>(Src.Num(), Expected);
			FMemory::Memcpy(MipData, Src.GetData(), CopyBytes);
		}
		PlatformData->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
	}

	return Texture;
}

void FVoxelRuntimeTextures::Shutdown()
{
	for (UTexture2D* Tex : RootedTextures)
	{
		if (IsValid(Tex))
		{
			Tex->RemoveFromRoot();
		}
	}
	RootedTextures.Reset();
	ByMaterial.Reset();
	SharedMetallic = nullptr;
	TerrainVCMaterial = nullptr;
	TerrainPBRMaterial = nullptr;
	bLoaded = false;
	bHasAnyCpuAlbedo = false;
}

const FVoxelRuntimeTextures::FMaterialTextures* FVoxelRuntimeTextures::Get(int32 MaterialId) const
{
	return ByMaterial.Find(MaterialId);
}

bool FVoxelRuntimeTextures::FMaterialTextures::HasCpuAlbedo() const
{
	if (AlbedoW <= 0 || AlbedoH <= 0)
	{
		return false;
	}
	const int64 Expected = static_cast<int64>(AlbedoW) * static_cast<int64>(AlbedoH) * 4;
	return AlbedoBGRA.Num() >= Expected && AlbedoBGRA.GetData() != nullptr;
}

FLinearColor FVoxelRuntimeTextures::SampleBGRA(const TArray64<uint8>& BGRA, int32 W, int32 H, float U, float V)
{
	if (W <= 0 || H <= 0 || BGRA.GetData() == nullptr)
	{
		return FLinearColor::Gray;
	}

	const int64 Expected = static_cast<int64>(W) * static_cast<int64>(H) * 4;
	if (BGRA.Num() < Expected)
	{
		return FLinearColor::Gray;
	}

	// Wrap into [0,1)
	U = FMath::Frac(U);
	V = FMath::Frac(V);
	if (U < 0.f) U += 1.f;
	if (V < 0.f) V += 1.f;

	const float X = U * static_cast<float>(W - 1);
	const float Y = V * static_cast<float>(H - 1);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, W - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, H - 1);
	const int32 X1 = FMath::Min(X0 + 1, W - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
	const float Fx = X - static_cast<float>(X0);
	const float Fy = Y - static_cast<float>(Y0);

	const uint8* Data = BGRA.GetData();
	auto Pix = [&](int32 PX, int32 PY) -> FLinearColor
	{
		const int64 O = (static_cast<int64>(PY) * W + PX) * 4;
		if (O < 0 || O + 3 >= BGRA.Num())
		{
			return FLinearColor::Gray;
		}
		const float B = Data[O + 0] / 255.0f;
		const float G = Data[O + 1] / 255.0f;
		const float R = Data[O + 2] / 255.0f;
		return FLinearColor(R, G, B, 1.0f);
	};

	return FMath::Lerp(FMath::Lerp(Pix(X0, Y0), Pix(X1, Y0), Fx), FMath::Lerp(Pix(X0, Y1), Pix(X1, Y1), Fx), Fy);
}

FLinearColor FVoxelRuntimeTextures::SampleAlbedoTriplanar(int32 MaterialId, const FVector& PlanetLocalMeters, const FVector& Normal, float Scale) const
{
	const FMaterialTextures* Set = Get(MaterialId);
	if (!Set)
	{
		// Fallback material table colors when texture missing
		return FLinearColor::Gray;
	}
	if (!Set->HasCpuAlbedo())
	{
		return Set->AverageColor;
	}

	FVector N = Normal;
	if (!N.Normalize())
	{
		N = FVector::UpVector;
	}
	const FVector Wabs(FMath::Abs(N.X), FMath::Abs(N.Y), FMath::Abs(N.Z));
	const float Sum = Wabs.X + Wabs.Y + Wabs.Z;
	if (Sum < KINDA_SMALL_NUMBER)
	{
		return Set->AverageColor;
	}
	const FVector Bw = Wabs / Sum;

	const float S = FMath::IsFinite(Scale) ? Scale : 0.35f;
	const FVector P = PlanetLocalMeters * S;
	if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
	{
		return Set->AverageColor;
	}

	const FLinearColor CX = SampleBGRA(Set->AlbedoBGRA, Set->AlbedoW, Set->AlbedoH, P.Y, P.Z);
	const FLinearColor CY = SampleBGRA(Set->AlbedoBGRA, Set->AlbedoW, Set->AlbedoH, P.X, P.Z);
	const FLinearColor CZ = SampleBGRA(Set->AlbedoBGRA, Set->AlbedoW, Set->AlbedoH, P.X, P.Y);

	return CX * Bw.X + CY * Bw.Y + CZ * Bw.Z;
}

void FVoxelRuntimeTextures::EnsureMaterials(UObject* Outer)
{
	if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Voxel/Materials/M_VoxelTerrain_VertexColor.M_VoxelTerrain_VertexColor")))
	{
		TerrainVCMaterial = Existing;
	}
	if (UMaterialInterface* ExistingPBR = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Voxel/Materials/M_VoxelTerrain_Triplanar.M_VoxelTerrain_Triplanar")))
	{
		TerrainPBRMaterial = ExistingPBR;
	}

	// NEVER DefaultTextMaterialOpaque — that is a font atlas (alphabet glyphs on terrain).
	if (!TerrainVCMaterial)
	{
		TerrainVCMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	}
	if (!TerrainVCMaterial)
	{
		TerrainVCMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}
	if (!TerrainVCMaterial)
	{
		TerrainVCMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	}
	if (!TerrainVCMaterial)
	{
		TerrainVCMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("Voxel terrain material: %s"), *GetNameSafe(TerrainVCMaterial));
}
