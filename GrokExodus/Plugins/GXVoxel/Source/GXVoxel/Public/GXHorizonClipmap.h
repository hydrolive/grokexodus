// Copyright Grok Exodus. All Rights Reserved.
// Far crust as a displaced height grid. Not UE Nanite — same stamp, coarse verts.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelStamps.h"
#include "GXCrustAtlas.h"
#include "ProceduralMeshComponent.h"

class AActor;
class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Rings of a spherical height mesh around the viewer.
 * Fills the sky past the voxel stream so mountains exist before you walk to them.
 */
class GXVOXEL_API FGXHorizonClipmap
{
public:
	void Initialize(AActor* Owner);
	void Shutdown();

	/** Rebuild when the viewer has moved. Safe to call every tick (cheap reject). */
	void Update(
		AActor* Owner,
		const FGXSphereStamp& Stamp,
		const FVector& ViewerLocalM,
		float InnerHoleM,
		float OuterM,
		UMaterialInterface* NearMaterial,
		UMaterialInterface* FarMaterial = nullptr,
		UMaterialInterface* PatchMaterial = nullptr,
		const FGXCrustAtlas* Atlas = nullptr,
		TFunction<bool(const FVector&)> ShouldPunch = nullptr,
		TFunction<float(const FVector&)> DensityAt = nullptr,
		TFunction<bool(const FVector&)> HasCaveMesh = nullptr);

	void Invalidate();
	/** Re-open saved air after a ring rebuild. */
	void NotifyEdits();
	/** Open the walk disk under this brush. Removes the grass lid. */
	void NotifyBrush(
		const FVector& LocalM,
		float RadiusM,
		TFunction<float(const FVector&)> DensityAt,
		TFunction<bool(const FVector&)> ShouldCut = nullptr);
	bool IsReady() const { return bReady; }

private:
	struct FRing
	{
		TWeakObjectPtr<UProceduralMeshComponent> Comp;
		TWeakObjectPtr<UMaterialInterface> Material;
		float InnerM = 0.0f;
		float OuterM = 0.0f;
		float CellM = 24.0f;
		float SinkM = 3.5f;
		float SinkUsed = 0.0f;
		FVector LastBuild = FVector(1e12f, 0, 0);
		TArray<FVector> StampPos;
		TArray<FVector> LivePos;
		TArray<FVector> StampDir;
		TArray<float> StampSurfM;
		TArray<FVector> LiveN;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> StampIndices;
		TArray<int32> LiveIndices;
		TArray<int32> GridOf;
		int32 GridDim = 0;
	};

	TArray<FRing> Rings;
	FVector LastViewerLocal = FVector(1e12f, 0, 0);
	double LastBrushSeconds = -1.0e9;
	bool bReady = false;
	bool bEditsDirty = false;

	void BuildRing(
		FRing& Ring,
		const FGXSphereStamp& Stamp,
		const FVector& CenterDir,
		const FVector& Tangent,
		const FVector& Bitangent,
		UMaterialInterface* Material,
		const FGXCrustAtlas* Atlas,
		const TFunction<float(const FVector&)>& DensityAt);

	void ApplyRingEdits(
		FRing& Ring,
		const FGXSphereStamp& Stamp,
		UMaterialInterface* Material,
		const TFunction<bool(const FVector&)>& ShouldCut,
		const TFunction<float(const FVector&)>& DensityAt,
		const TFunction<bool(const FVector&)>& HasCaveMesh);

	void OpenWalkRing(
		FRing& Ring,
		const FVector& BrushLocal,
		float BrushRadius,
		const TFunction<bool(const FVector&)>& ShouldCut,
		const TFunction<float(const FVector&)>& DensityAt);
};
