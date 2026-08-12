// Copyright Epic Games, Inc. All Rights Reserved.
// Spherical voxel planet: streaming, LOD, edit API, persistence.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelVolume.h"
#include "Voxel/VoxelSphereMapping.h"
#include "Voxel/VoxelRuntimeTextures.h"
#include "VoxelPlanetActor.generated.h"

class AVoxelChunkActor;
class UMaterialInterface;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct FVoxelLODBand
{
	GENERATED_BODY()

	/** Max distance from viewer (meters) to keep this LOD active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	float MaxDistance = 256.0f;

	/** LOD index (0 = finest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	int32 LOD = 0;
};

/**
 * Authoritative single-player planet volume + chunk streaming.
 * Place one in the level; player tools call Dig/Place on this actor.
 */
UCLASS(Blueprintable)
class AVoxelPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelPlanetActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual ~AVoxelPlanetActor() override;

	// ---- Config ----

	/**
	 * Planet radius in meters. Design target multi-km (SE-scale path).
	 * Performance comes from streaming + LOD, not shrinking the world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetRadius = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float MaxRelief = 180.0f;

	/** Base voxel edge length in meters. Keep at 1 for fine dig; finer later (0.5…). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float VoxelSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;

	/**
	 * Active stream radius (meters) around the viewer — not the planet size.
	 * Increase as LOD/async harden; this is the working set, not the world bound.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float StreamRadius = 256.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float UnloadRadius = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 MaxMeshBuildsPerFrame = 4;

	/** Burst mesh builds on spawn so collision exists before the player lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 WarmupMeshBuildsPerFrame = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float WarmupSeconds = 4.0f;

	/**
	 * If true, all streamed chunks mesh at full res (no LOD cracks, higher cost).
	 * Prefer false at planetary scale: LOD0 near player, coarser farther out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	bool bForceLOD0 = false;

	/** Cast dynamic shadows from terrain (expensive at scale — off by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bTerrainCastShadows = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	TArray<FVoxelLODBand> LODBands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Low-poly sphere visible from far away (planet silhouette). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rendering")
	TObjectPtr<UStaticMeshComponent> DistantPlanetSphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bShowDistantSphere = true;

	/** Hide impostor sphere when viewer is closer than this (meters from surface). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	float DistantSphereHideDistance = 180.0f;

	/**
	 * CPU-bake Grok Imagine albedo via triplanar into vertex colors.
	 * Default OFF — texture-sample path was crashing PIE; mesher already
	 * writes material debug colors. Enable only after textures verify clean.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bBakeTriplanarVertexColors = false;

	/** Texture tiling scale for triplanar bake (higher = smaller tiles). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	float TriplanarScale = 0.35f;

	/**
	 * Build meshes on worker threads.
	 * Default OFF — concurrent volume sampling corrupted heap during PIE.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	bool bAsyncMeshing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FString SaveFileName = TEXT("planet_default.gxvx");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	bool bAutoLoadOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	bool bAutoSaveOnEndPlay = true;

	// ---- Runtime API ----

	UFUNCTION(BlueprintCallable, Category = "Voxel|Gravity")
	FVector GetGravityDirectionAt(FVector WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Voxel|Gravity")
	FVector GetPlanetCenter() const { return GetActorLocation(); }

	UFUNCTION(BlueprintCallable, Category = "Voxel|Query")
	float SampleDensityWorld(FVector WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Voxel|Query")
	FVoxelHitResult RaycastVoxels(FVector WorldOrigin, FVector WorldDirection, float MaxDistance = 1000.0f) const;

	/**
	 * Dig (remove) with craftsmanship modifiers.
	 * Remeshes dirty chunks. Returns dig result for inventory/wear.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Edit")
	FVoxelDigResult DigSphere(FVector WorldCenter, float Radius, FVoxelToolModifiers Tool, float Strength = 1.0f);

	/** Place / fill material. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Edit")
	FVoxelDigResult PlaceSphere(FVector WorldCenter, float Radius, int32 MaterialId, FVoxelToolModifiers Tool, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Bunker")
	void RegisterBunkerVolumeWorld(FVector WorldCenter, FVector HalfExtents);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
	bool SavePlanet();

	UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
	bool LoadPlanet();

	/** Force stream around a world position (e.g. player). */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Streaming")
	void UpdateStreaming(FVector WorldViewerLocation);

	/** Process pending meshes immediately (spawn / surface place). */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Streaming")
	void FlushMeshQueue(int32 MaxBuilds = 64);

	/** World-cm surface point along a radial direction (density raycast). */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Query")
	FVector FindSurfaceWorldLocation(FVector RadialDirection) const;

	/** Stats for Phase 1 smoke documentation. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Debug")
	void GetStreamingStats(int32& OutLoadedChunks, int32& OutDirtyChunks, int64& OutMemoryBytes, float& OutLastMeshMs) const;

	FVoxelVolume& GetVolume() { return *Volume; }
	const FVoxelVolume& GetVolume() const { return *Volume; }

	/** UE world cm → planet-local meters. */
	FVector WorldToPlanetLocalMeters(const FVector& WorldCm) const;
	/** Planet-local meters → UE world cm. */
	FVector PlanetLocalMetersToWorld(const FVector& LocalM) const;

	// Back-compat names used by tools/game mode
	FVector WorldToPlanetLocal(const FVector& World) const { return WorldToPlanetLocalMeters(World); }
	FVector PlanetLocalToWorld(const FVector& Local) const { return PlanetLocalMetersToWorld(Local); }

protected:
	TUniquePtr<FVoxelVolume> Volume;

	/** Non-UPROPERTY: custom struct keys are awkward for UHT reflection. */
	TMap<FVoxelChunkCoord, TWeakObjectPtr<AVoxelChunkActor>> ChunkActors;

	TArray<FVoxelChunkCoord> MeshBuildQueue;
	TSet<FVoxelChunkCoord> MeshBuildQueued;

	float LastMeshBuildMs = 0.0f;
	double AccumMeshBuildMs = 0.0;
	int32 MeshBuildCount = 0;

	void RebuildPlanetParams();
	void EnqueueRemesh(const FVoxelChunkCoord& Coord);
	void ProcessMeshQueue(int32 Budget);
	void BuildChunkMesh(const FVoxelChunkCoord& Coord);
	void ApplyBuiltMesh(const FVoxelChunkCoord& Coord, int32 LOD, struct FVoxelMeshData&& MeshData, float BuildMs);
	void BakeTriplanarColors(struct FVoxelMeshData& MeshData) const;
	int32 SelectLOD(float Distance) const;
	FString GetSavePath() const;
	void UpdateDistantSphereVisual(const FVector& WorldViewerLocation);
	void SetupDistantSphere();
	UMaterialInterface* ResolveTerrainMaterial() const;

	UPROPERTY()
	TObjectPtr<APawn> TrackedPawn;

	float WarmupTimeRemaining = 0.0f;

	TUniquePtr<FVoxelRuntimeTextures> RuntimeTextures;

	/** Chunks currently meshing async — avoid double queue. */
	TSet<FVoxelChunkCoord> AsyncInFlight;
};
