// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GXInterfaces.h"
#include "GXJobGraph.h"
#include "GXVoxelStamps.h"
#include "GXVoxelTypes.h"
#include "GXMesher.h"
#include "GXVoxelVolume.h"
#include "GXVoxelWorld.generated.h"

class AGXVoxelChunkProxy;
class UGXVoxelInvokerComponent;
class UMaterialInterface;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct FGXVoxelHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	FVector Normal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	int32 MaterialId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	float Distance = 0.0f;
};

USTRUCT(BlueprintType)
struct FGXDigOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	int32 MaterialId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	float VolumeRemoved = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	float YieldAmount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GX")
	float ToolWear = 0.0f;
};

/**
 * Body-fixed voxel planet. Does not rotate or translate.
 * Streaming is invoker-driven. Meshing is snapshot + job graph.
 */
UCLASS()
class GXVOXEL_API AGXVoxelWorld : public AActor, public IGXVoxelQuery, public IGXGravityField
{
	GENERATED_BODY()

public:
	AGXVoxelWorld();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetRadius = 60000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float MaxRelief = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SurfaceG = 9.81f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float VoxelSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float StreamRadius = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float UnloadRadius = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float NearFieldRadius = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxMeshBuildsPerFrame = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 WarmupMeshBuildsPerFrame = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float WarmupSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	bool bAsyncMeshing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rendering")
	TObjectPtr<UStaticMeshComponent> DistantPlanetSphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FString SaveFileName = TEXT("earth_default.gxsav");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	bool bAutoLoadOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	bool bAutoSaveOnEndPlay = true;

	// IGXVoxelQuery
	virtual float SampleDensityMeters(const FVector3d& PlanetLocalMeters) const override;
	virtual int32 SampleMaterial(const FVector3d& PlanetLocalMeters) const override;

	// IGXGravityField
	virtual FVector GetGravityCmS2(const FVector& ScenePositionCm) const override;
	virtual FName GetBodyId() const override { return TEXT("Earth"); }

	UFUNCTION(BlueprintCallable, Category = "GX|Query")
	FVector GetGravityDirectionAt(FVector WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "GX|Query")
	float SampleDensityWorld(FVector WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "GX|Query")
	FGXVoxelHit RaycastVoxels(FVector WorldOrigin, FVector WorldDirection, float MaxDistance = 2000.0f) const;

	UFUNCTION(BlueprintCallable, Category = "GX|Query")
	FVector FindSurfaceWorldLocation(FVector RadialDirection) const;

	UFUNCTION(BlueprintCallable, Category = "GX|Edit")
	FGXDigOutcome DigSphere(FVector WorldCenter, float RadiusM, float DigSpeedMul = 1.0f, float RecoveryMul = 1.0f, float WearMul = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "GX|Edit")
	FGXDigOutcome PlaceSphere(FVector WorldCenter, float RadiusM, int32 MaterialId);

	UFUNCTION(BlueprintCallable, Category = "GX|Stream")
	void UpdateStreaming(FVector WorldViewerLocation);

	UFUNCTION(BlueprintCallable, Category = "GX|Stream")
	void FlushMeshQueue(int32 MaxBuilds = 64);

	UFUNCTION(BlueprintCallable, Category = "GX|Persist")
	bool SaveWorld();

	UFUNCTION(BlueprintCallable, Category = "GX|Persist")
	bool LoadWorld();

	FVector WorldToLocalMeters(const FVector& WorldCm) const;
	FVector LocalMetersToWorld(const FVector& LocalM) const;

	FGXVoxelVolume* GetVolume() const { return Volume.Get(); }

protected:
	TUniquePtr<FGXVoxelVolume> Volume;
	TUniquePtr<FGXJobGraph> Jobs;

	TMap<FGXChunkKey, TWeakObjectPtr<AGXVoxelChunkProxy>> ChunkActors;
	TArray<FGXChunkKey> MeshQueue;
	TSet<FGXChunkKey> MeshQueued;
	TSet<FGXChunkKey> AsyncInFlight;

	struct FPendingMesh
	{
		FGXChunkKey Coord;
		int32 LOD = 0;
		FGXGenerationStamp Stamp;
		FGXMeshBuffers Mesh;
	};
	FCriticalSection PendingCS;
	TArray<FPendingMesh> PendingMeshes;

	float WarmupTimeRemaining = 0.0f;

	void RebuildParams();
	void EnqueueRemesh(const FGXChunkKey& Coord);
	void ProcessMeshQueue(int32 Budget);
	void BuildChunkMeshSync(const FGXChunkKey& Coord);
	void EnqueueChunkMeshAsync(const FGXChunkKey& Coord);
	void DrainPendingMeshes(int32 Budget);
	void ApplyBuiltMesh(const FGXChunkKey& Coord, int32 LOD, FGXMeshBuffers&& MeshData);
	int32 SelectLOD(float DistanceM) const;
	FString GetSavePath() const;
	void SetupDistantSphere();
	FVector GetPrimaryInvokerLocation() const;
};
