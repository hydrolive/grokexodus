// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelPlanetActor.h"
#include "Voxel/VoxelChunkActor.h"
#include "Voxel/VoxelMesher.h"
#include "Voxel/VoxelPersistence.h"
#include "Voxel/VoxelMaterialTable.h"
#include "Voxel/VoxelRuntimeTextures.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Async/Async.h"
#include "UObject/ConstructorHelpers.h"

// UE world units are centimeters. Planet volume uses meters.
// Scale factor: world_cm = planet_m * 100.
static constexpr float GVoxelMetersToUU = 100.0f;
static constexpr float GVoxelUUToMeters = 0.01f;

AVoxelPlanetActor::~AVoxelPlanetActor()
{
	if (RuntimeTextures)
	{
		RuntimeTextures->Shutdown();
		RuntimeTextures.Reset();
	}
}

AVoxelPlanetActor::AVoxelPlanetActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetRoot"));
	SetRootComponent(Root);

	DistantPlanetSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DistantPlanetSphere"));
	DistantPlanetSphere->SetupAttachment(Root);
	DistantPlanetSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DistantPlanetSphere->SetCastShadow(true);
	DistantPlanetSphere->bCastDynamicShadow = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		DistantPlanetSphere->SetStaticMesh(SphereMesh.Object);
	}

	// Wide LOD0 ring so walkable ground has no LOD seams / missing collision.
	// Coarser only outside the playable near field.
	LODBands = {
		{ 140.0f, 0 },
		{ 200.0f, 1 },
		{ 280.0f, 2 },
		{ 400.0f, 3 }
	};
}

void AVoxelPlanetActor::RebuildPlanetParams()
{
	FVoxelPlanetParams Params;
	Params.Radius = PlanetRadius;
	Params.MaxRelief = MaxRelief;
	Params.VoxelSize = VoxelSize;
	Params.Seed = static_cast<uint32>(Seed);
	Params.CrustDepth = FMath::Max(8.0f, MaxRelief * 0.15f);
	Volume = MakeUnique<FVoxelVolume>(Params);
}

void AVoxelPlanetActor::SetupDistantSphere()
{
	if (!DistantPlanetSphere)
	{
		return;
	}

	// Engine BasicShapes/Sphere is 100 cm diameter (radius 50 cm).
	// Slightly smaller than crust so surface voxels cover it; holes won't flash void-black.
	const float WorldRadiusCm = PlanetRadius * GVoxelMetersToUU * 0.97f;
	const float Scale = WorldRadiusCm / 50.0f;
	DistantPlanetSphere->SetRelativeLocation(FVector::ZeroVector);
	DistantPlanetSphere->SetRelativeScale3D(FVector(Scale));
	DistantPlanetSphere->SetVisibility(bShowDistantSphere);
	DistantPlanetSphere->SetHiddenInGame(!bShowDistantSphere);

	// Bright earth fill under the voxel crust (never pure black)
	if (UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface))
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DefaultMat, this))
		{
			const FLinearColor Earth(0.42f, 0.48f, 0.32f, 1.0f);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Earth);
			MID->SetVectorParameterValue(TEXT("Color"), Earth);
			DistantPlanetSphere->SetMaterial(0, MID);
		}
	}
}

void AVoxelPlanetActor::UpdateDistantSphereVisual(const FVector& WorldViewerLocation)
{
	if (!DistantPlanetSphere || !bShowDistantSphere)
	{
		return;
	}

	const FVector LocalM = WorldToPlanetLocalMeters(WorldViewerLocation);
	const float DistFromCenterM = LocalM.Size();
	const float SurfaceDistM = DistFromCenterM - PlanetRadius;
	// Show sphere when far from surface; keep visible underfoot as base fill when medium-far
	const bool bShow = SurfaceDistM > DistantSphereHideDistance * 0.25f || DistFromCenterM > PlanetRadius * 1.5f;
	// Always keep sphere visible as planetary body (voxels overlay nearby crust)
	DistantPlanetSphere->SetVisibility(true);
	DistantPlanetSphere->SetHiddenInGame(false);
	(void)bShow;
}

void AVoxelPlanetActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildPlanetParams();
	SetupDistantSphere();
	WarmupTimeRemaining = WarmupSeconds;

	// NEVER load RuntimeTextures at startup (was part of crash path).
	// Mesher paints vertex colors; ResolveTerrainMaterial picks a safe material.
	if (!TerrainMaterial)
	{
		TerrainMaterial = ResolveTerrainMaterial();
	}

	if (bAutoLoadOnBeginPlay)
	{
		LoadPlanet();
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		TrackedPawn = PC->GetPawn();
	}

	// ALWAYS seed at crust surface — never at world origin (pawn often still sits there pre-place).
	// Origin-centered streaming wastes the mesh budget on empty deep-solid chunks.
	const FVector SeedViewer = GetStreamFocusWorldLocation();
	UpdateStreaming(SeedViewer);
	FlushMeshQueue(WarmupMeshBuildsPerFrame);
}

FVector AVoxelPlanetActor::GetStreamFocusWorldLocation() const
{
	const FVector Surface = FindSurfaceWorldLocation(FVector(1.0f, 0.0f, 0.0f));
	const FVector SurfaceUp = -GetGravityDirectionAt(Surface);

	if (TrackedPawn)
	{
		const FVector P = TrackedPawn->GetActorLocation();
		const float DistFromCenterM = WorldToPlanetLocalMeters(P).Size();
		// Trust the pawn only once it is near the crust (not default spawn / origin)
		if (DistFromCenterM > PlanetRadius * 0.5f)
		{
			return P;
		}
	}
	return Surface + SurfaceUp * 200.0f;
}

FVector AVoxelPlanetActor::FindSurfaceWorldLocation(FVector RadialDirection) const
{
	FVector Dir = RadialDirection.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector(1, 0, 0);
	}

	const FVector Center = GetActorLocation();
	const float OuterCm = (PlanetRadius + MaxRelief + 100.0f) * GVoxelMetersToUU;
	const FVector Start = Center + Dir * OuterCm;

	if (Volume)
	{
		const FVoxelHitResult Hit = RaycastVoxels(Start, -Dir, OuterCm + 2000.0f);
		if (Hit.bHit)
		{
			return Hit.Location;
		}
	}

	// Fallback: base radius along direction
	return Center + Dir * (PlanetRadius * GVoxelMetersToUU);
}

void AVoxelPlanetActor::FlushMeshQueue(int32 MaxBuilds)
{
	ProcessMeshQueue(FMath::Max(0, MaxBuilds));
}

void AVoxelPlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAutoSaveOnEndPlay && Volume)
	{
		SavePlanet();
	}
	if (RuntimeTextures)
	{
		RuntimeTextures->Shutdown();
		RuntimeTextures.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AVoxelPlanetActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Volume)
	{
		return;
	}

	if (WarmupTimeRemaining > 0.0f)
	{
		WarmupTimeRemaining -= DeltaSeconds;
	}

	if (!TrackedPawn)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			TrackedPawn = PC->GetPawn();
		}
	}

	const FVector Focus = GetStreamFocusWorldLocation();
	UpdateStreaming(Focus);
	UpdateDistantSphereVisual(Focus);

	// During warmup, spend almost everything on near field so ground appears under the player first
	const int32 Budget = (WarmupTimeRemaining > 0.0f)
		? WarmupMeshBuildsPerFrame
		: (MaxMeshBuildsPerFrame + NearMeshBuildsPerFrame);
	ProcessMeshQueue(Budget);
}

FVector AVoxelPlanetActor::WorldToPlanetLocalMeters(const FVector& WorldCm) const
{
	return (WorldCm - GetActorLocation()) * GVoxelUUToMeters;
}

FVector AVoxelPlanetActor::PlanetLocalMetersToWorld(const FVector& LocalM) const
{
	return GetActorLocation() + LocalM * GVoxelMetersToUU;
}

FVector AVoxelPlanetActor::GetGravityDirectionAt(FVector WorldPosition) const
{
	return FVoxelSphereMapping::GravityDirection(WorldToPlanetLocalMeters(WorldPosition));
}

float AVoxelPlanetActor::SampleDensityWorld(FVector WorldPosition) const
{
	if (!Volume)
	{
		return -1.0f;
	}
	// Density is in meters; return as-is (tooling uses meter brush radii)
	return Volume->SampleDensity(WorldToPlanetLocalMeters(WorldPosition));
}

FVoxelHitResult AVoxelPlanetActor::RaycastVoxels(FVector WorldOrigin, FVector WorldDirection, float MaxDistance) const
{
	FVoxelHitResult Hit;
	if (!Volume || MaxDistance <= 0.0f)
	{
		return Hit;
	}

	const FVector Dir = WorldDirection.GetSafeNormal();
	// MaxDistance / steps in UE cm
	const float StepCm = FMath::Max(VoxelSize * GVoxelMetersToUU * 0.35f, 25.0f);
	float PrevD = Volume->SampleDensity(WorldToPlanetLocalMeters(WorldOrigin));
	FVector PrevPos = WorldOrigin;

	for (float T = StepCm; T <= MaxDistance; T += StepCm)
	{
		const FVector Pos = WorldOrigin + Dir * T;
		const float D = Volume->SampleDensity(WorldToPlanetLocalMeters(Pos));
		// Enter solid from air: PrevD < 0 and D >= 0
		if (PrevD < 0.0f && D >= 0.0f)
		{
			// Binary refine
			float T0 = T - StepCm;
			float T1 = T;
			for (int32 I = 0; I < 8; ++I)
			{
				const float Tm = 0.5f * (T0 + T1);
				const float Dm = Volume->SampleDensity(WorldToPlanetLocalMeters(WorldOrigin + Dir * Tm));
				if (Dm >= 0.0f) T1 = Tm;
				else T0 = Tm;
			}
			const float THit = T1;
			const FVector HitPos = WorldOrigin + Dir * THit;
			const FVector LocalM = WorldToPlanetLocalMeters(HitPos);
			Hit.bHit = true;
			Hit.Location = HitPos;
			Hit.Distance = THit;
			Hit.VoxelCoord = Volume->GetMapping().WorldToVoxel(LocalM);
			Hit.MaterialId = Volume->SampleCell(LocalM).MaterialId;
			// Gradient normal in meter space
			const float E = VoxelSize * 0.5f;
			const float Dx = Volume->SampleDensity(LocalM + FVector(E, 0, 0)) - Volume->SampleDensity(LocalM - FVector(E, 0, 0));
			const float Dy = Volume->SampleDensity(LocalM + FVector(0, E, 0)) - Volume->SampleDensity(LocalM - FVector(0, E, 0));
			const float Dz = Volume->SampleDensity(LocalM + FVector(0, 0, E)) - Volume->SampleDensity(LocalM - FVector(0, 0, E));
			FVector N(-Dx, -Dy, -Dz);
			if (!N.Normalize())
			{
				N = -Dir;
			}
			Hit.Normal = N;
			return Hit;
		}
		PrevD = D;
		PrevPos = Pos;
	}
	return Hit;
}

FVoxelDigResult AVoxelPlanetActor::DigSphere(FVector WorldCenter, float Radius, FVoxelToolModifiers Tool, float Strength)
{
	FVoxelDigResult Result;
	if (!Volume)
	{
		return Result;
	}

	const FVector Local = WorldToPlanetLocalMeters(WorldCenter);
	const FVoxelVolume::FBrushResult Brush = Volume->ApplySphereBrush(Local, Radius, true, 0, Tool, Strength);
	Result.bSuccess = Brush.VolumeChanged > 0.0f;
	Result.VolumeRemoved = Brush.VolumeChanged;
	Result.MaterialId = Brush.DominantMaterialId;
	Result.YieldAmount = FVoxelMaterialTable::ComputeYield(
		Brush.VolumeChanged,
		Volume->GetMaterials().GetDigYield(Brush.DominantMaterialId),
		Tool);
	Result.ToolWear = FVoxelMaterialTable::ComputeWear(
		Brush.VolumeChanged,
		Volume->GetMaterials().GetWearFactor(Brush.DominantMaterialId),
		Tool);
	Result.DirtyChunks = Brush.DirtyChunks;

	for (const FVoxelChunkCoord& C : Brush.DirtyChunks)
	{
		EnqueueRemesh(C);
	}
	return Result;
}

FVoxelDigResult AVoxelPlanetActor::PlaceSphere(FVector WorldCenter, float Radius, int32 MaterialId, FVoxelToolModifiers Tool, float Strength)
{
	FVoxelDigResult Result;
	if (!Volume)
	{
		return Result;
	}

	const FVector Local = WorldToPlanetLocalMeters(WorldCenter);
	const FVoxelVolume::FBrushResult Brush = Volume->ApplySphereBrush(Local, Radius, false, MaterialId, Tool, Strength);
	Result.bSuccess = Brush.VolumeChanged > 0.0f;
	Result.VolumeRemoved = 0.0f;
	Result.MaterialId = MaterialId;
	Result.DirtyChunks = Brush.DirtyChunks;
	for (const FVoxelChunkCoord& C : Brush.DirtyChunks)
	{
		EnqueueRemesh(C);
	}
	return Result;
}

void AVoxelPlanetActor::RegisterBunkerVolumeWorld(FVector WorldCenter, FVector HalfExtents)
{
	if (!Volume)
	{
		return;
	}
	const FVector Local = WorldToPlanetLocalMeters(WorldCenter);
	const FVector HalfM = HalfExtents * GVoxelUUToMeters;
	const FBox Box(Local - HalfM, Local + HalfM);
	Volume->RegisterBunkerVolume(Box);
}

int32 AVoxelPlanetActor::ClaimBunkerWorld(FVector WorldCenter, FVector HalfExtentsCm, bool bAutoSave)
{
	if (!Volume)
	{
		return 0;
	}
	RegisterBunkerVolumeWorld(WorldCenter, HalfExtentsCm);

	// Remesh overlapping chunks so bunker state is authoritative on disk/load
	const FVector Local = WorldToPlanetLocalMeters(WorldCenter);
	const FVector HalfM = HalfExtentsCm * GVoxelUUToMeters;
	const FBox Box(Local - HalfM, Local + HalfM);
	const FIntVector MinV = Volume->GetMapping().WorldToVoxel(Box.Min);
	const FIntVector MaxV = Volume->GetMapping().WorldToVoxel(Box.Max);
	const FVoxelChunkCoord MinC = FVoxelSphereMapping::VoxelToChunk(MinV);
	const FVoxelChunkCoord MaxC = FVoxelSphereMapping::VoxelToChunk(MaxV);
	for (int32 Z = MinC.Z; Z <= MaxC.Z; ++Z)
	{
		for (int32 Y = MinC.Y; Y <= MaxC.Y; ++Y)
		{
			for (int32 X = MinC.X; X <= MaxC.X; ++X)
			{
				EnqueueRemesh(FVoxelChunkCoord(X, Y, Z), true);
			}
		}
	}

	const int32 Protected = Volume->CountBunkerCells(Box);
	if (bAutoSave)
	{
		SavePlanet();
	}
	UE_LOG(LogVoxelWorld, Log, TEXT("Claimed bunker at %s half=%s cells=%d saved=%s"),
		*WorldCenter.ToString(), *HalfExtentsCm.ToString(), Protected, bAutoSave ? TEXT("yes") : TEXT("no"));
	return Protected;
}

int32 AVoxelPlanetActor::CountBunkerCellsWorld(FVector WorldCenter, FVector HalfExtentsCm) const
{
	if (!Volume)
	{
		return 0;
	}
	const FVector Local = WorldToPlanetLocalMeters(WorldCenter);
	const FVector HalfM = HalfExtentsCm * GVoxelUUToMeters;
	return Volume->CountBunkerCells(FBox(Local - HalfM, Local + HalfM));
}

FString AVoxelPlanetActor::GetSavePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelWorld"), SaveFileName);
}

bool AVoxelPlanetActor::SavePlanet()
{
	if (!Volume)
	{
		return false;
	}
	return FVoxelPersistence::SaveToFile(*Volume, GetSavePath());
}

bool AVoxelPlanetActor::LoadPlanet()
{
	if (!Volume)
	{
		RebuildPlanetParams();
	}
	const FString Path = GetSavePath();
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	const bool bOk = FVoxelPersistence::LoadFromFile(*Volume, Path);
	if (bOk)
	{
		// Remesh all dirty + currently loaded
		TArray<FVoxelChunkCoord> Dirty;
		Volume->GetDirtyChunkCoords(Dirty);
		for (const FVoxelChunkCoord& C : Dirty)
		{
			EnqueueRemesh(C);
		}
		for (const auto& Pair : ChunkActors)
		{
			EnqueueRemesh(Pair.Key);
		}
	}
	return bOk;
}

int32 AVoxelPlanetActor::SelectLOD(float Distance) const
{
	// LOD mismatches between adjacent chunks create visible cracks/holes
	if (bForceLOD0)
	{
		return 0;
	}
	int32 Best = 0;
	for (const FVoxelLODBand& Band : LODBands)
	{
		if (Distance <= Band.MaxDistance)
		{
			return Band.LOD;
		}
		Best = FMath::Max(Best, Band.LOD);
	}
	return Best;
}

void AVoxelPlanetActor::UpdateStreaming(FVector WorldViewerLocation)
{
	if (!Volume)
	{
		return;
	}

	// Stream radii are authored in meters on the actor; convert viewer to meters.
	const FVector LocalViewerM = WorldToPlanetLocalMeters(WorldViewerLocation);
	const float ChunkWorldM = Volume->GetMapping().ChunkWorldSize(0);
	const int32 ChunkRadius = FMath::CeilToInt(StreamRadius / ChunkWorldM) + 1;
	const float ChunkHalfDiag = ChunkWorldM * 0.8660254f; // ~sqrt(3)/2 * edge

	// Only mesh the crust shell (surface ± relief). Pure deep-solid / pure air waste the mesh budget
	// and leave the player with empty near-field while distant sphere still draws.
	const float ShellInner = PlanetRadius - MaxRelief - 24.0f;
	const float ShellOuter = PlanetRadius + MaxRelief + 16.0f;

	const FVoxelChunkCoord CenterChunk = FVoxelSphereMapping::VoxelToChunk(
		Volume->GetMapping().WorldToVoxel(LocalViewerM));

	const float NearR = FMath::Clamp(NearFieldRadius, 32.0f, StreamRadius);

	TSet<FVoxelChunkCoord> Desired;
	for (int32 Z = -ChunkRadius; Z <= ChunkRadius; ++Z)
	{
		for (int32 Y = -ChunkRadius; Y <= ChunkRadius; ++Y)
		{
			for (int32 X = -ChunkRadius; X <= ChunkRadius; ++X)
			{
				const FVoxelChunkCoord CC(CenterChunk.X + X, CenterChunk.Y + Y, CenterChunk.Z + Z);
				const FVector ChunkOriginM = Volume->GetMapping().ChunkOriginWorld(CC);
				const FVector ChunkCenterM = ChunkOriginM + FVector(ChunkWorldM * 0.5f);
				const float Dist = FVector::Dist(ChunkCenterM, LocalViewerM);
				if (Dist > StreamRadius)
				{
					continue;
				}

				const float R = ChunkCenterM.Size();
				const float RMin = FMath::Max(0.0f, R - ChunkHalfDiag);
				const float RMax = R + ChunkHalfDiag;

				const bool bNearPlayer = Dist <= NearR;
				const bool bIntersectsShell = (RMax >= ShellInner) && (RMin <= ShellOuter);
				if (!bIntersectsShell && !bNearPlayer)
				{
					continue;
				}
				if (RMax < ShellInner && Dist > ChunkWorldM * 1.25f)
				{
					continue;
				}

				Desired.Add(CC);

				const int32 WantLOD = SelectLOD(Dist);
				if (!ChunkActors.Contains(CC))
				{
					Volume->GetOrCreateChunk(CC);
					FActorSpawnParameters SP;
					SP.Owner = this;
					SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AVoxelChunkActor* ChunkActor = GetWorld()->SpawnActor<AVoxelChunkActor>(
						AVoxelChunkActor::StaticClass(),
						GetActorLocation(),
						FRotator::ZeroRotator,
						SP);
					if (ChunkActor)
					{
						ChunkActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
						ChunkActor->SetActorRelativeLocation(FVector::ZeroVector);
						ChunkActor->InitializeChunk(CC, WantLOD);
						ChunkActors.Add(CC, ChunkActor);
						EnqueueRemesh(CC, bNearPlayer);
					}
				}
				else if (TWeakObjectPtr<AVoxelChunkActor>* Existing = ChunkActors.Find(CC))
				{
					if (AVoxelChunkActor* CA = Existing->Get())
					{
						if (CA->LOD != WantLOD && !AsyncInFlight.Contains(CC)
							&& !MeshBuildQueued.Contains(CC) && !NearMeshQueued.Contains(CC))
						{
							if (WantLOD < CA->LOD || Dist > NearR + 16.0f)
							{
								CA->LOD = WantLOD;
								EnqueueRemesh(CC, bNearPlayer);
							}
						}
					}
				}
			}
		}
	}

	TArray<FVoxelChunkCoord> ToUnload;
	for (const auto& Pair : ChunkActors)
	{
		const FVector ChunkCenterM = Volume->GetMapping().ChunkOriginWorld(Pair.Key)
			+ FVector(ChunkWorldM * 0.5f);
		const float Dist = FVector::Dist(ChunkCenterM, LocalViewerM);
		if (Dist > UnloadRadius && !Desired.Contains(Pair.Key))
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FVoxelChunkCoord& CC : ToUnload)
	{
		if (TWeakObjectPtr<AVoxelChunkActor>* Found = ChunkActors.Find(CC))
		{
			if (AVoxelChunkActor* Actor = Found->Get())
			{
				Actor->Destroy();
			}
		}
		ChunkActors.Remove(CC);
		MeshBuildQueued.Remove(CC);
		NearMeshQueued.Remove(CC);
	}
	if (ToUnload.Num() > 0)
	{
		MeshBuildQueue.RemoveAll([&](const FVoxelChunkCoord& C) { return ToUnload.Contains(C); });
		NearMeshQueue.RemoveAll([&](const FVoxelChunkCoord& C) { return ToUnload.Contains(C); });
	}

	Volume->UnloadUnusedChunks(Desired);
}

void AVoxelPlanetActor::EnqueueRemesh(const FVoxelChunkCoord& Coord, bool bNearPriority)
{
	if (MeshBuildQueued.Contains(Coord) || NearMeshQueued.Contains(Coord))
	{
		// Promote to near queue if requested
		if (bNearPriority && MeshBuildQueued.Contains(Coord) && !NearMeshQueued.Contains(Coord))
		{
			MeshBuildQueue.Remove(Coord);
			MeshBuildQueued.Remove(Coord);
			NearMeshQueued.Add(Coord);
			NearMeshQueue.Add(Coord);
		}
		return;
	}

	if (bNearPriority)
	{
		NearMeshQueued.Add(Coord);
		NearMeshQueue.Add(Coord);
	}
	else
	{
		MeshBuildQueued.Add(Coord);
		MeshBuildQueue.Add(Coord);
	}
}

void AVoxelPlanetActor::ProcessOneMeshQueue(
	TArray<FVoxelChunkCoord>& Queue,
	TSet<FVoxelChunkCoord>& QueuedSet,
	int32& Built,
	int32 Budget)
{
	while (Built < Budget && Queue.Num() > 0)
	{
		const FVoxelChunkCoord Coord = Queue[0];
		Queue.RemoveAt(0, 1, EAllowShrinking::No);
		QueuedSet.Remove(Coord);
		if (ChunkActors.Contains(Coord) && !AsyncInFlight.Contains(Coord))
		{
			BuildChunkMesh(Coord);
			++Built;
		}
	}
}

void AVoxelPlanetActor::ProcessMeshQueue(int32 Budget)
{
	int32 Built = 0;

	// 1) Always drain near-field first (underfoot / around player)
	const int32 NearBudget = FMath::Max(NearMeshBuildsPerFrame, Budget);
	ProcessOneMeshQueue(NearMeshQueue, NearMeshQueued, Built, NearBudget);

	// 2) Remaining budget (or full far budget) for outer stream
	const int32 FarBudget = Budget + FMath::Max(0, NearBudget - Built);
	if (Built < FarBudget && MeshBuildQueue.Num() > 0 && Volume)
	{
		// Light partial sort of far queue head toward viewer
		const FVector ViewerM = WorldToPlanetLocalMeters(GetStreamFocusWorldLocation());
		const float ChunkWorldM = Volume->GetMapping().ChunkWorldSize(0);
		const int32 SortN = FMath::Min(MeshBuildQueue.Num(), 24);
		for (int32 I = 0; I < SortN; ++I)
		{
			int32 Best = I;
			float BestD = TNumericLimits<float>::Max();
			for (int32 J = I; J < MeshBuildQueue.Num(); ++J)
			{
				const FVector C = Volume->GetMapping().ChunkOriginWorld(MeshBuildQueue[J])
					+ FVector(ChunkWorldM * 0.5f);
				const float D = FVector::DistSquared(C, ViewerM);
				if (D < BestD)
				{
					BestD = D;
					Best = J;
				}
			}
			if (Best != I)
			{
				MeshBuildQueue.Swap(I, Best);
			}
		}
	}
	ProcessOneMeshQueue(MeshBuildQueue, MeshBuildQueued, Built, FarBudget);
}

UMaterialInterface* AVoxelPlanetActor::ResolveTerrainMaterial() const
{
	// Prefer explicitly assigned material
	if (TerrainMaterial)
	{
		// Guard: never keep the font/text material if someone assigned it by mistake
		const FString Name = TerrainMaterial->GetName();
		if (!Name.Contains(TEXT("Text"), ESearchCase::IgnoreCase)
			&& !Name.Contains(TEXT("Font"), ESearchCase::IgnoreCase))
		{
			return TerrainMaterial;
		}
	}

	// Project material (created by create_voxel_terrain_material.py)
	if (UMaterialInterface* ProjectMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Voxel/Materials/M_VoxelTerrain_VertexColor.M_VoxelTerrain_VertexColor")))
	{
		return ProjectMat;
	}

	// Engine debug materials that display vertex color as albedo (solid landscape tints)
	if (UMaterialInterface* VC = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly")))
	{
		return VC;
	}
	if (UMaterialInterface* VC2 = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial")))
	{
		return VC2;
	}

	// Flat prototype material from FP template (solid color, not glyphs)
	if (UMaterialInterface* Flat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol")))
	{
		return Flat;
	}

	return UMaterial::GetDefaultMaterial(MD_Surface);
}

void AVoxelPlanetActor::BakeTriplanarColors(FVoxelMeshData& MeshData) const
{
	// Permanently disabled for stability. Texture CPU sampling caused AVs in PIE.
	// Landscape colors come from the mesher (material debug vertex colors).
	(void)MeshData;
	return;
}

void AVoxelPlanetActor::ApplyBuiltMesh(const FVoxelChunkCoord& Coord, int32 LOD, FVoxelMeshData&& MeshData, float BuildMs)
{
	TWeakObjectPtr<AVoxelChunkActor>* FoundActor = ChunkActors.Find(Coord);
	AVoxelChunkActor* ChunkActor = FoundActor ? FoundActor->Get() : nullptr;
	if (!ChunkActor)
	{
		AsyncInFlight.Remove(Coord);
		return;
	}

	LastMeshBuildMs = BuildMs;
	AccumMeshBuildMs += BuildMs;
	++MeshBuildCount;

	// Mesh positions are planet-local meters → UE cm relative to planet actor root.
	TArray<FVector> LocalPositions;
	LocalPositions.Reserve(MeshData.Positions.Num());
	for (const FVector& P : MeshData.Positions)
	{
		LocalPositions.Add(P * GVoxelMetersToUU);
	}

	// Collision on LOD0 and LOD1 so you never fall through coarser near-field crust
	const bool bCollision = (LOD <= 1);
	ChunkActor->LOD = LOD;
	// Solid-color multi-section mesh (BasicShapeMaterial MIDs) — never black
	UMaterialInterface* ParentMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!ParentMat)
	{
		ParentMat = ResolveTerrainMaterial();
	}
	ChunkActor->ApplyMeshData(
		LocalPositions,
		MeshData.Indices,
		MeshData.Normals,
		MeshData.UV0,
		MeshData.Colors,
		MeshData.Tangents,
		MeshData.MaterialIds,
		ParentMat,
		bCollision,
		bTerrainCastShadows);

	if (FVoxelChunk* Chunk = Volume ? Volume->FindChunk(Coord) : nullptr)
	{
		Chunk->bMeshDirty = false;
	}

	AsyncInFlight.Remove(Coord);

	UE_LOG(LogVoxelWorld, Verbose, TEXT("Meshed chunk (%d,%d,%d) LOD%d: %d verts, %d tris, %.2f ms"),
		Coord.X, Coord.Y, Coord.Z, LOD,
		MeshData.Positions.Num(),
		MeshData.Indices.Num() / 3,
		BuildMs);
}

void AVoxelPlanetActor::BuildChunkMesh(const FVoxelChunkCoord& Coord)
{
	if (!Volume)
	{
		return;
	}
	TWeakObjectPtr<AVoxelChunkActor>* FoundActor = ChunkActors.Find(Coord);
	AVoxelChunkActor* ChunkActor = FoundActor ? FoundActor->Get() : nullptr;
	if (!ChunkActor)
	{
		return;
	}

	// Ensure data present
	Volume->GetOrCreateChunk(Coord);

	// Distance-based LOD for this chunk (forced LOD0 avoids crack-holes)
	int32 LOD = 0;
	if (!bForceLOD0)
	{
		if (TrackedPawn)
		{
			const float ChunkWorldM = Volume->GetMapping().ChunkWorldSize(0);
			const FVector ChunkCenterM = Volume->GetMapping().ChunkOriginWorld(Coord) + FVector(ChunkWorldM * 0.5f);
			const FVector ViewerM = WorldToPlanetLocalMeters(TrackedPawn->GetActorLocation());
			LOD = SelectLOD(FVector::Dist(ChunkCenterM, ViewerM));
		}
		else
		{
			LOD = ChunkActor->LOD;
		}
	}

	FVoxelMesher::FSettings Settings;
	Settings.bGenerateCollision = (LOD <= 1);
	Settings.bVertexColorsFromMaterial = true;
	Settings.LOD = LOD;

	// Async mesh generation (no texture bake on worker — bake on game thread only).
	// Warmup stays fully synchronous so collision exists under the player.
	const bool bUseAsync = bAsyncMeshing && WarmupTimeRemaining <= 0.0f;

	if (bUseAsync)
	{
		AsyncInFlight.Add(Coord);

		FVoxelVolume* VolPtr = Volume.Get();
		TWeakObjectPtr<AVoxelPlanetActor> WeakThis(this);
		const int32 CaptureLOD = LOD;

		Async(EAsyncExecution::ThreadPool, [WeakThis, Coord, Settings, VolPtr, CaptureLOD]()
		{
			if (!VolPtr)
			{
				return;
			}

			const double T0 = FPlatformTime::Seconds();
			FVoxelMeshData MeshData = FVoxelMesher::MeshChunk(*VolPtr, Coord, Settings);
			const float Ms = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Coord, CaptureLOD, MeshData = MoveTemp(MeshData), Ms]() mutable
			{
				if (AVoxelPlanetActor* Self = WeakThis.Get())
				{
					// Game-thread only: safe access to RuntimeTextures
					Self->BakeTriplanarColors(MeshData);
					Self->ApplyBuiltMesh(Coord, CaptureLOD, MoveTemp(MeshData), Ms);
				}
			});
		});
		return;
	}

	// Sync path (warmup / fallback)
	const double T0 = FPlatformTime::Seconds();
	FVoxelMeshData MeshData = FVoxelMesher::MeshChunk(*Volume, Coord, Settings);
	BakeTriplanarColors(MeshData);
	const float Ms = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
	ApplyBuiltMesh(Coord, LOD, MoveTemp(MeshData), Ms);
}

void AVoxelPlanetActor::GetStreamingStats(int32& OutLoadedChunks, int32& OutDirtyChunks, int64& OutMemoryBytes, float& OutLastMeshMs) const
{
	OutLoadedChunks = ChunkActors.Num();
	OutDirtyChunks = 0;
	OutMemoryBytes = Volume ? Volume->GetAllocatedMemoryBytes() : 0;
	OutLastMeshMs = LastMeshBuildMs;
	if (Volume)
	{
		TArray<FVoxelChunkCoord> Dirty;
		Volume->GetDirtyChunkCoords(Dirty);
		OutDirtyChunks = Dirty.Num();
	}
}
