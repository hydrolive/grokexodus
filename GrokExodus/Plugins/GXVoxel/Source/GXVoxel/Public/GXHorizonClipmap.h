// Copyright Grok Exodus. All Rights Reserved.
// Far crust as a displaced height grid. Not UE Nanite — same stamp, coarse verts.
#pragma once

#include "CoreMinimal.h"
#include "GXVoxelStamps.h"
#include "GXCrustAtlas.h"

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
		const FGXCrustAtlas* Atlas = nullptr,
		const TArray<FVector4>* EditHolesLocalM = nullptr,
		TFunction<float(const FVector&)> DensityAt = nullptr);

	void Invalidate();
	/** Rebuild only the local brush patches. Do not recut the 8 m crust. */
	void NotifyEdits();
	bool IsReady() const { return bReady; }

private:
	struct FRing
	{
		TWeakObjectPtr<UProceduralMeshComponent> Comp;
		float InnerM = 0.0f;
		float OuterM = 0.0f;
		float CellM = 24.0f;
		float SinkM = 3.5f;
		FVector LastBuild = FVector(1e12f, 0, 0);
	};

	TArray<FRing> Rings;
	TWeakObjectPtr<UProceduralMeshComponent> EditPatch;
	FVector LastViewerLocal = FVector(1e12f, 0, 0);
	bool bReady = false;
	bool bEditsDirty = false;

	static void BuildRing(
		UProceduralMeshComponent* Comp,
		const FGXSphereStamp& Stamp,
		const FVector& CenterDir,
		const FVector& Tangent,
		const FVector& Bitangent,
		float InnerM,
		float OuterM,
		float CellM,
		float SinkM,
		UMaterialInterface* Material,
		const FGXCrustAtlas* Atlas,
		const TArray<FVector4>* EditHolesLocalM,
		const TFunction<float(const FVector&)>& DensityAt);

	static void BuildEditPatch(
		UProceduralMeshComponent* Comp,
		const FGXSphereStamp& Stamp,
		const FVector& ViewerLocalM,
		UMaterialInterface* Material,
		const TArray<FVector4>* EditHolesLocalM);
};
