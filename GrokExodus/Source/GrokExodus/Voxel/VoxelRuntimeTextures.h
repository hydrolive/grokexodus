// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 3: load Grok Imagine PBR sets from disk and CPU-sample them (triplanar).

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelMaterialTable.h"

class UTexture2D;
class UMaterialInterface;
class UObject;

/**
 * Loads Content/Voxel/Textures/Source JPGs into CPU buffers (+ optional transient UTexture2Ds),
 * provides safe CPU triplanar albedo sampling for vertex baking.
 */
class FVoxelRuntimeTextures
{
public:
	struct FMaterialTextures
	{
		TObjectPtr<UTexture2D> Albedo = nullptr;
		TObjectPtr<UTexture2D> Normal = nullptr;
		TObjectPtr<UTexture2D> Roughness = nullptr;
		TObjectPtr<UTexture2D> Metallic = nullptr;
		/** CPU-side BGRA bytes for albedo. */
		TArray64<uint8> AlbedoBGRA;
		int32 AlbedoW = 0;
		int32 AlbedoH = 0;
		FLinearColor AverageColor = FLinearColor::Gray;

		bool HasCpuAlbedo() const;
	};

	void Initialize(UObject* Outer);
	void Shutdown();
	bool IsReady() const { return bLoaded; }
	bool HasCpuAlbedoData() const { return bHasAnyCpuAlbedo; }

	const FMaterialTextures* Get(int32 MaterialId) const;
	UTexture2D* GetSharedMetallic() const { return SharedMetallic; }

	/** CPU triplanar sample of albedo (never crashes; returns gray/average on failure). */
	FLinearColor SampleAlbedoTriplanar(int32 MaterialId, const FVector& PlanetLocalMeters, const FVector& Normal, float Scale = 0.35f) const;

	UMaterialInterface* GetTerrainVertexColorMaterial() const { return TerrainVCMaterial; }
	UMaterialInterface* GetTerrainPBRMaterial() const { return TerrainPBRMaterial; }

	static FString GetSourceTextureDir();

private:
	bool bLoaded = false;
	bool bHasAnyCpuAlbedo = false;
	TMap<int32, FMaterialTextures> ByMaterial;
	TObjectPtr<UTexture2D> SharedMetallic = nullptr;
	TObjectPtr<UMaterialInterface> TerrainVCMaterial = nullptr;
	TObjectPtr<UMaterialInterface> TerrainPBRMaterial = nullptr;
	TObjectPtr<UObject> OuterPtr = nullptr;
	/** Keep CreateTransient textures alive across GC. */
	TArray<TObjectPtr<UTexture2D>> RootedTextures;

	UTexture2D* LoadTextureFile(const FString& AbsolutePath, bool bNormalMap, TArray64<uint8>* OutBGRA, int32* OutW, int32* OutH);
	void LoadMaterialSet(int32 MaterialId, const TCHAR* BaseName);
	void EnsureMaterials(UObject* Outer);
	static FLinearColor SampleBGRA(const TArray64<uint8>& BGRA, int32 W, int32 H, float U, float V);
};
