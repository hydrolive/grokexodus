// Copyright Grok Exodus. All Rights Reserved.
// Loads Imagine PBR sets and binds a triplanar blend MID.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UTexture2DArray;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UObject;

class GXVOXEL_API FGXTerrainPBR
{
public:
	void Initialize(UObject* Outer);
	void Shutdown();

	UMaterialInterface* GetMaterial() const;
	UMaterialInterface* GetPatchMaterial() const;

	static FString GetSourceDir();

private:
	bool bReady = false;
	TObjectPtr<UObject> OuterPtr;
	TObjectPtr<UTexture2DArray> AlbedoArray;
	TObjectPtr<UTexture2DArray> NormalArray;
	TObjectPtr<UTexture2DArray> RoughArray;
	TObjectPtr<UTexture2D> AlbedoAtlas;
	TObjectPtr<UTexture2D> NormalAtlas;
	TObjectPtr<UTexture2D> RoughAtlas;
	TObjectPtr<UMaterialInstanceDynamic> Mid;
	TObjectPtr<UMaterialInterface> Applied;
	TArray<TObjectPtr<UObject>> Roots;

	bool LoadJpg(const FString& Path, TArray<uint8>& OutBGRA, int32& OutW, int32& OutH);
	UTexture2DArray* BuildArray(const TArray<TArray<uint8>>& Slices, const TArray<FIntPoint>& Sizes, bool bNormal, const TCHAR* Name);
	UTexture2D* BuildAtlas(const TArray<TArray<uint8>>& Slices, const TArray<FIntPoint>& Sizes, bool bNormal, const TCHAR* Name);
	void ResizeBGRA(const TArray<uint8>& Src, int32 SW, int32 SH, TArray<uint8>& Dst, int32 DW, int32 DH) const;
};
