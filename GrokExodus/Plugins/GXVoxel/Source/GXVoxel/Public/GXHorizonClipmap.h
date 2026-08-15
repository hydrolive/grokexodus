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
		const TArray<FVector4>* EditHolesLocalM = nullptr,
		TFunction<float(const FVector&)> DensityAt = nullptr);

	void Invalidate();
	/** Rebuild only the circular brush patch. Do not remesh the walk ring. */
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
		float SinkUsed = 0.0f;
		FVector LastBuild = FVector(1e12f, 0, 0);
		TArray<FVector> StampPos;
		TArray<FVector> StampDir;
		TArray<float> StampSurfM;
		TArray<FVector> LiveN;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
	};

	TArray<FRing> Rings;
	TWeakObjectPtr<UProceduralMeshComponent> EditPatch;
	FVector LastViewerLocal = FVector(1e12f, 0, 0);
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

	void ApplyRingEdits(FRing& Ring, const TArray<FVector4>* Edits);

	static void BuildEditPatch(
		UProceduralMeshComponent* Comp,
		const FGXSphereStamp& Stamp,
		UMaterialInterface* Material,
		const TArray<FVector4>* EditHolesLocalM);
};
