// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelWorld.h"
#include "GXMesher.h"
#include "GXVoxelChunkProxy.h"
#include "ProceduralMeshComponent.h"
#include "GXVoxelInvokerComponent.h"
#include "GXVoxelVolume.h"
#include "GXGravity.h"
#include "GXMath.h"
#include "GXVoxel.h"
#include "GXVersion.h"
#include "GXPerf.h"
#include "HAL/FileManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "GXBodyMovement.h"
#include "GXCrustAtlas.h"
#include "GXCrustCache.h"
#include "Async/Async.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"

static constexpr float GMetersToUU = 100.0f;
static constexpr float GUUToMeters = 0.01f;

namespace GXPersist
{
	static constexpr uint32 Magic = 0x31565847; // GXV1
	/** 3 = island as explicit float xyzr (v2 wrote 4 bytes of FVector doubles). */
	static constexpr uint32 Version = 3;
}

AGXVoxelWorld::AGXVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DistantPlanetSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DistantSphere"));
	DistantPlanetSphere->SetupAttachment(Root);
	DistantPlanetSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DistantPlanetSphere->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		DistantPlanetSphere->SetStaticMesh(SphereMesh.Object);
	}
}

void AGXVoxelWorld::RebuildParams()
{
	FGXPlanetStampParams P = FGXPlanetStampParams::Earth();
	P.Radius = PlanetRadius;
	P.MaxRelief = MaxRelief;
	P.VoxelSize = VoxelSize;
	P.Seed = static_cast<uint32>(Seed);
	P.CrustDepth = FMath::Max(24.0f, MaxRelief * 0.04f);
	Volume = MakeUnique<FGXVoxelVolume>(P);
	Jobs = MakeUnique<FGXJobGraph>();
}

void AGXVoxelWorld::ResetStreamingState()
{
	if (Jobs)
	{
		Jobs->BumpStamp();
	}
	if (MeshMailbox)
	{
		MeshMailbox->bAlive.Store(false);
		FScopeLock Lock(&MeshMailbox->CS);
		MeshMailbox->Pending.Empty();
	}
	MeshMailbox = MakeShared<FGXMeshMailbox, ESPMode::ThreadSafe>();
	CrustAtlas.Reset();
	bAtlasReady = false;
	bAtlasBuildInFlight = false;
	ActiveStreamRadius = 140.0f;
	NearMeshQueue.Empty();
	CacheHits = 0;
	CacheMisses = 0;
	for (auto& Pair : ChunkActors)
	{
		if (AGXVoxelChunkProxy* Proxy = Pair.Value.Get())
		{
			Proxy->Destroy();
		}
	}
	ChunkActors.Empty();
	TArray<FGXChunkKey> VisKeys;
	ChunkVisuals.GetKeys(VisKeys);
	for (const FGXChunkKey& K : VisKeys)
	{
		ReleaseVisual(K);
	}
	ChunkVisuals.Empty();
	MeshQueue.Empty();
	MeshQueued.Empty();
	AsyncInFlight.Empty();
	HollowChunks.Empty();
	EmptyRetries.Empty();
	LastRemeshAt.Empty();
	NextEmptyRetryAt.Empty();
	EditedPageBoxesM.Empty();
	CaveChunks.Empty();
	CarveBalls.Empty();
	bPersistDirty = false;
	AutoSaveAccum = 0.0f;
	LastSettledEmpty = 0;
	LastHollowNear = 0;
	StallSeconds = 0;
	RemeshWhenIdle.Empty();
	BrushForceLOD0.Empty();
	{
		FScopeLock Lock(&PendingCS);
		PendingMeshes.Empty();
	}
	LastStreamViewerWorld = FVector(1e12f, 0, 0);
	WarmupTimeRemaining = WarmupSeconds;
	bWorldReady = false;
	bRevealedTileEdits = false;
	bLoadRestorePending = false;
	LastRestoreTileCount = 0;
	LoadProgress = 0.0f;
	LoadStatus = TEXT("Rebuilding planet…");
}

void AGXVoxelWorld::ConfigurePlanet(float InRadius, float InRelief, float InStream, int32 InSeed)
{
	const bool bChanged =
		!FMath::IsNearlyEqual(PlanetRadius, InRadius)
		|| !FMath::IsNearlyEqual(MaxRelief, InRelief)
		|| !FMath::IsNearlyEqual(StreamRadius, InStream)
		|| (InSeed != 0 && Seed != InSeed)
		|| !Volume;

	PlanetRadius = InRadius;
	MaxRelief = InRelief;
	StreamRadius = FMath::Clamp(InStream, 120.0f, 900.0f);
	UnloadRadius = StreamRadius + 220.0f;
	NearFieldRadius = FMath::Clamp(NearFieldRadius, 80.0f, StreamRadius * 0.5f);
	CollisionRadius = FMath::Max(CollisionRadius, NearFieldRadius);
	if (InSeed != 0)
	{
		Seed = InSeed;
	}

	if (!bChanged && Volume)
	{
		return;
	}

	if (HasActorBegunPlay())
	{
		ResetStreamingState();
		RebuildParams();
		SetupDistantSphere();
		if (Foliage)
		{
			Foliage->Clear();
		}
		UE_LOG(LogGXVoxel, Warning,
			TEXT("GX-%s ConfigurePlanet R=%.0f relief=%.0f stream=%.0f"),
			GX_VERSION_STRING, PlanetRadius, MaxRelief, StreamRadius);
	}
}

void AGXVoxelWorld::ApplyEarthPlayDefaults()
{
	const FGXPlanetStampParams E = FGXPlanetStampParams::Earth();
	StreamRadius = 140.0f;
	UnloadRadius = 260.0f;
	NearFieldRadius = 90.0f;
	CollisionRadius = 90.0f;
	StreamInterval = 0.45f;
	// 0.8: transvoxel skirts stitch LOD0/1. Keep detail underfoot.
	bForceLOD0 = false;
	bDrawVoxelVisuals = false;
	HorizonOuterM = 10000.0f;
	bAsyncMeshing = true;
	WarmupSeconds = 1.5f;
	WarmupMeshBuildsPerFrame = 4;
	MaxMeshBuildsPerFrame = 2;
	MeshTimeBudgetMs = 6.0f;
	MaxMeshCreatesPerTick = 1;
	MaxAsyncInFlight = 16;
	bAutoLoadOnBeginPlay = true;
	ConfigurePlanet(E.Radius, E.MaxRelief, StreamRadius, static_cast<int32>(E.Seed));
	if (HasActorBegunPlay() && bAutoLoadOnBeginPlay)
	{
		LoadWorld();
	}
}

void AGXVoxelWorld::SetupDistantSphere()
{
	if (!DistantPlanetSphere)
	{
		return;
	}
	// Engine sphere mesh radius is 50 cm. Scale to mean planet radius so the
	// limb matches the crust. Near pixels are clipped in M_VoxelHorizon so
	// holes do not show a second grass layer.
	const float WorldRadiusCm = PlanetRadius * GMetersToUU;
	DistantPlanetSphere->SetRelativeScale3D(FVector(WorldRadiusCm / 50.0f));
	DistantPlanetSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DistantPlanetSphere->SetCastShadow(false);
	DistantPlanetSphere->SetVisibleInRayTracing(false);
	if (UMaterialInterface* Horizon = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Voxel/Materials/M_VoxelHorizon.M_VoxelHorizon")))
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Horizon, this))
		{
			MID->SetScalarParameterValue(TEXT("HorizonNearCm"), StreamRadius * 100.0f * 0.8f);
			DistantPlanetSphere->SetMaterial(0, MID);
		}
		else
		{
			DistantPlanetSphere->SetMaterial(0, Horizon);
		}
		DistantPlanetSphere->SetVisibility(true);
		DistantPlanetSphere->SetHiddenInGame(false);
	}
	// Mean-radius sphere hid every peak. Clipmap is the far terrain.
	DistantPlanetSphere->SetVisibility(false);
	DistantPlanetSphere->SetHiddenInGame(true);
}

void AGXVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s AGXVoxelWorld BeginPlay radius=%.0f stream=%.0f async=%d"),
		GX_VERSION_STRING, PlanetRadius, StreamRadius, bAsyncMeshing ? 1 : 0);
	RebuildParams();
	MeshMailbox = MakeShared<FGXMeshMailbox, ESPMode::ThreadSafe>();
	SetupDistantSphere();
	WarmupTimeRemaining = WarmupSeconds;
	ActiveStreamRadius = 140.0f;
	LoadStatus = TEXT("Preparing planet…");
	LoadProgress = 0.03f;
	TerrainPBR = MakeUnique<FGXTerrainPBR>();
	TerrainPBR->Initialize(this);
	Foliage = MakeUnique<FGXFoliageScatter>();
	Foliage->Initialize(this);
	HorizonClipmap = MakeUnique<FGXHorizonClipmap>();
	HorizonClipmap->Initialize(this);
	CrustTiles = MakeUnique<FGXCrustTiles>();
	CrustTiles->Initialize(this);
	EnsureMeshBanks();
	if (UMaterialInterface* PBR = TerrainPBR->GetMaterial())
	{
		TerrainMaterial = PBR;
		UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s terrain material=%s"),
			GX_VERSION_STRING, *GetNameSafe(TerrainMaterial));
	}
	else if (UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Voxel/Materials/M_VoxelTerrain_VertexColor.M_VoxelTerrain_VertexColor")))
	{
		TerrainMaterial = VC;
		UE_LOG(LogGXVoxel, Warning, TEXT("GXTerrainPBR missing — using vertex-color material"));
	}

	if (bAutoLoadOnBeginPlay)
	{
		LoadWorld();
	}

	// Do not mesh here. BeginPlay must return so the Slate overlay can tick.
	EnsureCrustAtlas();
}

void AGXVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAutoSaveOnEndPlay)
	{
		SaveWorld();
	}
	if (MeshMailbox)
	{
		MeshMailbox->bAlive.Store(false);
	}
	TArray<FGXChunkKey> VisKeys;
	ChunkVisuals.GetKeys(VisKeys);
	for (const FGXChunkKey& K : VisKeys)
	{
		ReleaseVisual(K);
	}
	for (TObjectPtr<UProceduralMeshComponent>& Bank : MeshBanks)
	{
		if (Bank)
		{
			Bank->DestroyComponent();
		}
	}
	MeshBanks.Reset();
	FreeVisualSlots.Reset();
	if (HorizonClipmap)
	{
		HorizonClipmap->Shutdown();
		HorizonClipmap.Reset();
		if (CrustTiles)
		{
			CrustTiles->Shutdown();
			CrustTiles.Reset();
		}
	}
	if (Foliage)
	{
		Foliage->Shutdown();
		Foliage.Reset();
	}
	if (TerrainPBR)
	{
		TerrainPBR->Shutdown();
		TerrainPBR.Reset();
	}
	if (Jobs)
	{
		// Do not wait on long Earth-field jobs — they check mailbox->bAlive and exit.
		Jobs->Flush(0.25f);
	}
	Super::EndPlay(EndPlayReason);
}

void AGXVoxelWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Volume)
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	if (WarmupTimeRemaining > 0.0f)
	{
		WarmupTimeRemaining -= DeltaSeconds;
	}

	CachedViewerWorld = GetPrimaryInvokerLocation();
	EnsureCrustAtlas();

	if (bAtlasReady && ActiveStreamRadius < StreamRadius)
	{
		ActiveStreamRadius = FMath::Min(StreamRadius, ActiveStreamRadius + FMath::Max(40.0f, StreamRadius * 0.22f));
	}

	StreamCooldown -= DeltaSeconds;
	double StreamMs = 0.0;
	const float MovedSq = FVector::DistSquared(CachedViewerWorld, LastStreamViewerWorld);
	// Do not stream every warmup frame — that stamped 2300 jobs in 1 s and
	// left the async pool wedged on empty remeshes.
	if (bAtlasReady && (StreamCooldown <= 0.0f || MovedSq > FMath::Square(600.0f)))
	{
		const double S0 = FPlatformTime::Seconds();
		UpdateStreaming(CachedViewerWorld);
		LastStreamViewerWorld = CachedViewerWorld;
		StreamCooldown = StreamInterval;
		StreamMs = (FPlatformTime::Seconds() - S0) * 1000.0;
	}

	const double M0 = FPlatformTime::Seconds();
	MeshCreatesThisTick = 0;
	const int32 QueueBefore = NearMeshQueue.Num() + MeshQueue.Num();
	// Edited-chunk caves must remesh even when the unedited crust is clipmap-only.
	DrainPendingMeshes(MaxMeshBuildsPerFrame);
	const int32 Budget = (WarmupTimeRemaining > 0.0f) ? WarmupMeshBuildsPerFrame : MaxMeshBuildsPerFrame;
	ProcessMeshQueue(Budget);
	const double MeshMs = (FPlatformTime::Seconds() - M0) * 1000.0;
	if (CrustTiles && Volume && bAtlasReady)
	{
		CrustTiles->Update(
			this,
			Volume->GetStamp(),
			WorldToLocalMeters(CachedViewerWorld),
			TerrainMaterial.Get(),
			(WarmupTimeRemaining > 0.0f) ? 9 : 2,
			[this](const FVector& P)
			{
				return SampleDensityMeters(FVector3d(P.X, P.Y, P.Z));
			});
		// Load-only. A live dig must not restore the whole 8 m page box
		// (GX-shot-0139 hide-air r=6.04 punched the lawn).
		if (bLoadRestorePending && CrustTiles->IsReady())
		{
			RestoreEditedSurfaces();
		}
	}
	if (HorizonClipmap && Volume && bAtlasReady)
	{
		// 5×5 tiles = 320 m square. A 140 m circle stuck out past a 4×4
		// (0.9.9 live: hole through the planet). Hole 100 m only after 5×5.
		const float ClipInnerM = (CrustTiles && CrustTiles->HasNeighborhood(
			WorldToLocalMeters(CachedViewerWorld), 2))
			? 100.0f
			: 0.0f;
		HorizonClipmap->Update(
			this,
			Volume->GetStamp(),
			WorldToLocalMeters(CachedViewerWorld),
			ClipInnerM,
			HorizonOuterM,
			TerrainMaterial.Get(),
			TerrainMaterial.Get(),
			TerrainPBR ? TerrainPBR->GetPatchMaterial() : TerrainMaterial.Get(),
			CrustAtlas.Get(),
			[this](const FVector& LocalM)
			{
				return ShouldPunchClipmap(LocalM);
			},
			[this](const FVector& LocalM)
			{
				return SampleDensityMeters(FVector3d(LocalM.X, LocalM.Y, LocalM.Z));
			},
			[this](const FVector& LocalM)
			{
				if (!Volume)
				{
					return false;
				}
				const FGXChunkKey Key = FGXVoxelVolume::VoxelToChunk(
					FGXVoxelVolume::WorldToVoxel(FVector3d(LocalM.X, LocalM.Y, LocalM.Z), VoxelSize));
				return ChunkVisuals.Contains(Key);
			});
	}
	if (bWorldReady && bPersistDirty && AutoSaveIntervalSeconds > 0.0f)
	{
		AutoSaveAccum += DeltaSeconds;
		if (AutoSaveAccum >= AutoSaveIntervalSeconds)
		{
			AutoSaveAccum = 0.0f;
			SaveWorld();
		}
	}
	if (Foliage && Volume && bWorldReady)
	{
		Foliage->Sync(this, Volume->GetStamp(), CachedViewerWorld, PlanetRadius);
	}
	RefreshLoadState();

	const double TickMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	static double LogAcc = 0.0;
	LogAcc += DeltaSeconds;
	if (LogAcc >= 1.0)
	{
		LogAcc = 0.0;
		int32 MailboxN = 0;
		if (MeshMailbox.IsValid())
		{
			FScopeLock Lock(&MeshMailbox->CS);
			MailboxN = MeshMailbox->Pending.Num();
		}
		const int32 InF = AsyncInFlight.Num();
		const int32 QAfter = NearMeshQueue.Num() + MeshQueue.Num();
		if (InF > 0 && InF == LastInFlightLogged && QAfter >= QueueBefore - 1 && !bWorldReady)
		{
			++StallSeconds;
		}
		else
		{
			StallSeconds = 0;
		}
		LastInFlightLogged = InF;
		UE_LOG(LogGXVoxel, Warning,
			TEXT("GX-%s perf tick=%.1fms stream=%.1fms meshApply=%.1fms dt=%.1fms fps~%.1f chunks=%d hollow=%d near=%d/%d settled=%d queue=%d->%d inflight=%d jobs=%d mailbox=%d cache=%d/%d ready=%d status=%s playerR=%.0fm"),
			GX_VERSION_STRING,
			TickMs, StreamMs, MeshMs,
			DeltaSeconds * 1000.0,
			DeltaSeconds > KINDA_SMALL_NUMBER ? 1.0 / DeltaSeconds : 0.0,
			ChunkVisuals.Num(), HollowChunks.Num(),
			LastMeshedNear, LastDesiredNear, LastSettledEmpty,
			QueueBefore, QAfter,
			InF, Jobs ? Jobs->NumInFlight() : 0, MailboxN,
			CacheHits, CacheMisses,
			bWorldReady ? 1 : 0,
			*LoadStatus,
			WorldToLocalMeters(CachedViewerWorld).Size());
		if (StallSeconds >= 3)
		{
			GX_PERF(1, TEXT("GX-mesh STALL %ds inflight=%d jobs=%d mailbox=%d nearQ=%d farQ=%d desired=%d meshed=%d hollow=%d"),
				StallSeconds, InF, Jobs ? Jobs->NumInFlight() : 0, MailboxN,
				NearMeshQueue.Num(), MeshQueue.Num(),
				LastDesiredNear, LastMeshedNear, HollowChunks.Num());
		}
	}
}

FVector AGXVoxelWorld::GetPrimaryInvokerLocation() const
{
	if (UWorld* World = GetWorld())
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			return Pawn->GetActorLocation();
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const UGXVoxelInvokerComponent* Inv = It->FindComponentByClass<UGXVoxelInvokerComponent>())
			{
				if (Inv->bEnabled)
				{
					return Inv->GetInvokerWorldLocation();
				}
			}
		}
	}
	if (!CachedViewerWorld.IsNearlyZero())
	{
		return CachedViewerWorld;
	}
	return LocalMetersToWorld(FVector(PlanetRadius + 2.0f, 0.0f, 0.0f));
}

FVector AGXVoxelWorld::WorldToLocalMeters(const FVector& WorldCm) const
{
	return (WorldCm - GetActorLocation()) * GUUToMeters;
}

FVector AGXVoxelWorld::LocalMetersToWorld(const FVector& LocalM) const
{
	return GetActorLocation() + LocalM * GMetersToUU;
}

float AGXVoxelWorld::SampleDensityMeters(const FVector3d& PlanetLocalMeters) const
{
	FGXVoxelPacked Stored;
	if (Volume && Volume->TryGetAuthoritative(PlanetLocalMeters, Stored))
	{
		return Stored.ToDensityMeters();
	}
	if (CrustAtlas.IsValid())
	{
		float Dens = 0.0f;
		uint8 Mat = 0;
		if (CrustAtlas->TrySample(PlanetLocalMeters, Dens, Mat))
		{
			return Dens;
		}
	}
	return Volume ? Volume->GetStamp().SampleDensity(PlanetLocalMeters) : -1.0f;
}

int32 AGXVoxelWorld::SampleMaterial(const FVector3d& PlanetLocalMeters) const
{
	FGXVoxelPacked Stored;
	if (Volume && Volume->TryGetAuthoritative(PlanetLocalMeters, Stored))
	{
		return Stored.Material;
	}
	if (CrustAtlas.IsValid())
	{
		float Dens = 0.0f;
		uint8 Mat = 0;
		if (CrustAtlas->TrySample(PlanetLocalMeters, Dens, Mat))
		{
			return static_cast<int32>(Mat);
		}
	}
	return Volume ? Volume->GetStamp().SampleMaterial(PlanetLocalMeters, 1.0f) : 0;
}

FVector AGXVoxelWorld::GetGravityCmS2(const FVector& ScenePositionCm) const
{
	const FVector3d LocalM = FVector3d(WorldToLocalMeters(ScenePositionCm));
	const double Mu = FGXGravity::SurfaceMu(SurfaceG, PlanetRadius);
	const FVector3d AccM = FGXGravity::Acceleration(LocalM, Mu);
	return FVector(AccM.X, AccM.Y, AccM.Z) * GMetersToUU;
}

FVector AGXVoxelWorld::GetGravityDirectionAt(FVector WorldPosition) const
{
	const FVector G = GetGravityCmS2(WorldPosition);
	return G.GetSafeNormal();
}

float AGXVoxelWorld::SampleDensityWorld(FVector WorldPosition) const
{
	const FVector L = WorldToLocalMeters(WorldPosition);
	return SampleDensityMeters(FVector3d(L.X, L.Y, L.Z));
}

FGXVoxelHit AGXVoxelWorld::RaycastVoxels(FVector WorldOrigin, FVector WorldDirection, float MaxDistance) const
{
	FGXVoxelHit Hit;
	if (!Volume || MaxDistance <= 0.0f)
	{
		return Hit;
	}
	const FGXSphereStamp& Stamp = Volume->GetStamp();
	const float R0 = Stamp.GetParams().Radius;
	auto StampSurfM = [&](const FVector& LocalM) -> float
	{
		FVector Rad = LocalM.GetSafeNormal();
		if (Rad.IsNearlyZero())
		{
			return R0;
		}
		const FGXEarthField F = Stamp.SampleEarthField(FVector3f(Rad.X, Rad.Y, Rad.Z), false);
		return R0 + F.HeightM;
	};
	// Authoritative cells only — a padded page AABB was a fake wall at the
	// page face (~3 m down) so the tool could not reach the next solid.
	// A grass lid over saved air is not a hit: walk the column to the
	// excavated floor so the ball sits in the hole (0.8.20 #1/#2).
	auto ColumnFloorM = [&](const FVector& LocalM) -> float
	{
		FVector Rad = LocalM.GetSafeNormal();
		if (Rad.IsNearlyZero())
		{
			return StampSurfM(LocalM);
		}
		const float Surf = StampSurfM(LocalM);
		float DeepAir = -1.0f;
		FGXVoxelPacked Stored;
		for (float D = 0.0f; D <= 48.0f; D += 1.0f)
		{
			const FVector3d P(Rad.X * (Surf - D), Rad.Y * (Surf - D), Rad.Z * (Surf - D));
			if (Volume->TryGetAuthoritative(P, Stored) && Stored.ToDensityMeters() <= 0.0f)
			{
				DeepAir = Surf - D;
			}
		}
		if (DeepAir < 0.0f)
		{
			return Surf;
		}
		float R = DeepAir;
		for (int32 I = 0; I < 16; ++I)
		{
			const FVector3d P(Rad.X * R, Rad.Y * R, Rad.Z * R);
			if (Volume->TryGetAuthoritative(P, Stored))
			{
				if (Stored.ToDensityMeters() > 0.0f)
				{
					break;
				}
			}
			else if (R < Surf - 0.5f)
			{
				break;
			}
			R -= 0.25f;
			if (R < Surf - 48.0f)
			{
				break;
			}
		}
		return R;
	};
	float CachedFloor = -1.0f;
	auto AboveVisual = [&](const FVector& WorldCm) -> bool
	{
		const FVector L = WorldToLocalMeters(WorldCm);
		FGXVoxelPacked Stored;
		if (Volume->TryGetAuthoritative(FVector3d(L.X, L.Y, L.Z), Stored))
		{
			return Stored.ToDensityMeters() <= 0.0f;
		}
		if (CachedFloor < 0.0f)
		{
			CachedFloor = ColumnFloorM(L);
		}
		return L.Size() > CachedFloor;
	};

	const FVector Dir = WorldDirection.GetSafeNormal();
	// Cave MC first so the ball cannot sit behind a leftover lid (0134).
	if (CaveChunks.Num() > 0 && ChunkVisuals.Num() > 0)
	{
		const FTransform Xf = GetActorTransform();
		const FVector LO = Xf.InverseTransformPosition(WorldOrigin);
		const FVector LD = Xf.InverseTransformVectorNoScale(Dir).GetSafeNormal();
		float BestT = MaxDistance;
		FVector BestN = FVector::ZeroVector;
		bool bCaveHit = false;
		auto RayTri = [](const FVector& Orig, const FVector& D, const FVector& A,
			const FVector& B, const FVector& C, float MaxT, float& OutT, FVector& OutN) -> bool
		{
			const FVector E1 = B - A;
			const FVector E2 = C - A;
			const FVector P = FVector::CrossProduct(D, E2);
			const float Det = FVector::DotProduct(E1, P);
			if (FMath::Abs(Det) < 1.0e-10f)
			{
				return false;
			}
			const float Inv = 1.0f / Det;
			const FVector T = Orig - A;
			const float U = FVector::DotProduct(T, P) * Inv;
			if (U < 0.0f || U > 1.0f)
			{
				return false;
			}
			const FVector Q = FVector::CrossProduct(T, E1);
			const float V = FVector::DotProduct(D, Q) * Inv;
			if (V < 0.0f || U + V > 1.0f)
			{
				return false;
			}
			const float Tt = FVector::DotProduct(E2, Q) * Inv;
			if (Tt <= 1.0e-4f || Tt >= MaxT)
			{
				return false;
			}
			OutT = Tt;
			OutN = FVector::CrossProduct(E1, E2).GetSafeNormal();
			return true;
		};
		for (const auto& Pair : ChunkVisuals)
		{
			if (!CaveChunks.Contains(Pair.Key) || Pair.Value.IndexCount < 3)
			{
				continue;
			}
			UProceduralMeshComponent* PMC = MeshBanks.IsValidIndex(Pair.Value.Bank)
				? MeshBanks[Pair.Value.Bank].Get() : nullptr;
			if (!PMC)
			{
				continue;
			}
			const FProcMeshSection* Sec = PMC->GetProcMeshSection(Pair.Value.Section);
			if (!Sec)
			{
				continue;
			}
			const TArray<FProcMeshVertex>& Verts = Sec->ProcVertexBuffer;
			const TArray<uint32>& Idx = Sec->ProcIndexBuffer;
			for (int32 T = 0; T + 2 < Idx.Num(); T += 3)
			{
				if (!Verts.IsValidIndex(Idx[T]) || !Verts.IsValidIndex(Idx[T + 1])
					|| !Verts.IsValidIndex(Idx[T + 2]))
				{
					continue;
				}
				float HitT = 0.0f;
				FVector HitN = FVector::ZeroVector;
				if (RayTri(LO, LD, Verts[Idx[T]].Position, Verts[Idx[T + 1]].Position,
					Verts[Idx[T + 2]].Position, BestT, HitT, HitN))
				{
					BestT = HitT;
					BestN = HitN;
					bCaveHit = true;
				}
			}
		}
		if (bCaveHit)
		{
			const FVector HitPos = WorldOrigin + Dir * BestT;
			const FVector Local = WorldToLocalMeters(HitPos);
			if (FVector::DotProduct(BestN, -Dir) < 0.0f)
			{
				BestN = -BestN;
			}
			Hit.bHit = true;
			Hit.Location = HitPos;
			Hit.Distance = BestT;
			Hit.MaterialId = SampleMaterial(FVector3d(Local.X, Local.Y, Local.Z));
			Hit.Normal = BestN.IsNearlyZero() ? -Dir : BestN;
			return Hit;
		}
	}
	if (CrustTiles && CrustTiles->IsReady())
	{
		FVector HitPos = FVector::ZeroVector;
		FVector HitN = FVector::ZeroVector;
		if (CrustTiles->RaycastVisible(WorldOrigin, Dir, MaxDistance, HitPos, HitN))
		{
			const FVector Local = WorldToLocalMeters(HitPos);
			FVector N = HitN.IsNearlyZero() ? Local.GetSafeNormal() : HitN.GetSafeNormal();
			if (N.IsNearlyZero())
			{
				N = -Dir;
			}
			// Skip leftover lid / look-through: air 0.55 m behind the hit
			// means this face is not the next solid (GX-shot-0131).
			const FVector Probe = Local - N * 0.55f;
			const float Behind = SampleDensityMeters(FVector3d(Probe.X, Probe.Y, Probe.Z));
			if (Behind > 0.0f)
			{
				Hit.bHit = true;
				Hit.Location = HitPos;
				Hit.Distance = FVector::Dist(WorldOrigin, HitPos);
				Hit.MaterialId = SampleMaterial(FVector3d(Local.X, Local.Y, Local.Z));
				Hit.Normal = N;
				return Hit;
			}
		}
	}
	const float StepCm = 12.0f;
	bool bPrevAbove = AboveVisual(WorldOrigin);
	for (float T = StepCm; T <= MaxDistance; T += StepCm)
	{
		const FVector Pos = WorldOrigin + Dir * T;
		const bool bAbove = AboveVisual(Pos);
		if (bPrevAbove && !bAbove)
		{
			float T0 = T - StepCm, T1 = T;
			for (int32 I = 0; I < 8; ++I)
			{
				const float Tm = 0.5f * (T0 + T1);
				if (AboveVisual(WorldOrigin + Dir * Tm)) T0 = Tm;
				else T1 = Tm;
			}
			const FVector HitPos = WorldOrigin + Dir * T1;
			const FVector Local = WorldToLocalMeters(HitPos);
			Hit.bHit = true;
			Hit.Location = HitPos;
			Hit.Distance = T1;
			Hit.MaterialId = SampleMaterial(FVector3d(Local.X, Local.Y, Local.Z));
			FVector N = Local.GetSafeNormal();
			if (N.IsNearlyZero()) N = -Dir;
			Hit.Normal = N;
			return Hit;
		}
		bPrevAbove = bAbove;
	}
	return Hit;
}

FVector AGXVoxelWorld::FindSurfaceWorldLocation(FVector RadialDirection) const
{
	FVector Dir = RadialDirection.GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = FVector(1, 0, 0);
	const FVector3f D(Dir.X, Dir.Y, Dir.Z);
	float SurfaceR = PlanetRadius;
	if (CrustAtlas.IsValid())
	{
		float Dens = 0.0f;
		uint8 Mat = 0;
		const FVector3d Probe(Dir.X * PlanetRadius, Dir.Y * PlanetRadius, Dir.Z * PlanetRadius);
		if (CrustAtlas->TrySample(Probe, Dens, Mat))
		{
			SurfaceR = PlanetRadius + Dens;
		}
	}
	else if (Volume)
	{
		SurfaceR = Volume->GetStamp().SampleSurfaceRadius(D);
	}
	return GetActorLocation() + Dir * (SurfaceR * GMetersToUU);
}

FGXDigOutcome AGXVoxelWorld::DigSphere(FVector WorldCenter, float RadiusM, float DigSpeedMul, float RecoveryMul, float WearMul, FVector HitNormal)
{
	FGXDigOutcome Out;
	if (!Volume)
	{
		return Out;
	}
	const FVector L = WorldToLocalMeters(WorldCenter);
	const FGXVoxelVolume::FBrushResult Brush = Volume->ApplySphereBrush(
		FVector3d(L.X, L.Y, L.Z), RadiusM * DigSpeedMul, true, 0, 1.0f);
	Out.bSuccess = Brush.VolumeChanged > 0.0f;
	Out.VolumeRemoved = Brush.VolumeChanged;
	Out.MaterialId = Brush.DominantMaterialId;
	Out.YieldAmount = Brush.VolumeChanged * 0.7f * RecoveryMul;
	Out.ToolWear = Brush.VolumeChanged * WearMul;
	if (Jobs) Jobs->BumpStamp();
	for (const FGXChunkKey& C : Brush.DirtyChunks)
	{
		FGXCrustCache::InvalidateChunk(Volume->GetStamp().GetParams(), C);
	}
	int32 Punched = 0;
	int32 Closed = 0;
	const float BrushR = RadiusM * DigSpeedMul;
	EditIsland.Add(L, BrushR + FGXEditIsland::CollarM);
	{
		const int32 SavedCreates = MaxMeshCreatesPerTick;
		MaxMeshCreatesPerTick = MeshCreatesThisTick + 8;
		RemeshIsland();
		MaxMeshCreatesPerTick = SavedCreates;
	}
	int32 CaveTris = 0;
	{
		const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
		for (const FGXChunkKey& K : CaveChunks)
		{
			const FVector C((K.X + 0.5f) * ChunkM, (K.Y + 0.5f) * ChunkM, (K.Z + 0.5f) * ChunkM);
			const FBox Box(C - FVector(ChunkM * 0.5f), C + FVector(ChunkM * 0.5f));
			if (!EditIsland.OverlapsBox(Box))
			{
				continue;
			}
			if (const FChunkVisual* V = ChunkVisuals.Find(K))
			{
				CaveTris += V->IndexCount / 3;
			}
		}
	}
	if (CrustTiles && CaveTris >= 12)
	{
		const FBox IB = EditIsland.Bounds();
		const FVector IC = IB.IsValid ? IB.GetCenter() : L;
		const float IR = IB.IsValid ? IB.GetExtent().GetMax() : (BrushR + FGXEditIsland::CollarM);
		Punched = CrustTiles->ConsumeWhere(
			IC, IR,
			[this](const FVector& P) { return EditIsland.Contains(P); },
			TerrainMaterial.Get());
	}
	else
	{
		GX_PERF(1, TEXT("GX-island miss tris=%d — lid stays"), CaveTris);
	}
	(void)HitNormal;
	(void)Closed;
	GX_PERF(1, TEXT("GX-island spheres=%d remesh-tris=%d consume=%d r=%.2f"),
		EditIsland.Spheres.Num(), CaveTris, Punched, BrushR + FGXEditIsland::CollarM);
	{
		static double LastBoxesAt = -1.0e9;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastBoxesAt > 0.75)
		{
			RebuildEditedPageBoxes();
			LastBoxesAt = Now;
		}
	}
	MarkPersistDirty();
	if (HorizonClipmap && !(CrustTiles && CrustTiles->HasTileAt(L)))
	{
		HorizonClipmap->NotifyBrush(
			L,
			RadiusM * DigSpeedMul,
			[this](const FVector& P)
			{
				return SampleDensityMeters(FVector3d(P.X, P.Y, P.Z));
			},
			[this](const FVector& P)
			{
				return ShouldPunchClipmap(P);
			},
			true);
	}
	GX_PERF(1, TEXT("GX-dig pages local=(%.1f,%.1f,%.1f) r=%.2f dirty=%d consume=%d spheres=%d boxes=%d"),
		L.X, L.Y, L.Z, BrushR, Brush.DirtyChunks.Num(), Punched, EditIsland.Spheres.Num(),
		EditedPageBoxesM.Num());
	return Out;
}

FGXDigOutcome AGXVoxelWorld::PlaceSphere(FVector WorldCenter, float RadiusM, int32 MaterialId)
{
	FGXDigOutcome Out;
	if (!Volume)
	{
		return Out;
	}
	const FVector L = WorldToLocalMeters(WorldCenter);
	const FGXVoxelVolume::FBrushResult Brush = Volume->ApplySphereBrush(
		FVector3d(L.X, L.Y, L.Z), RadiusM, false, static_cast<uint8>(FMath::Clamp(MaterialId, 1, 12)), 1.0f);
	Out.bSuccess = Brush.VolumeChanged > 0.0f;
	Out.MaterialId = MaterialId;
	if (Jobs) Jobs->BumpStamp();
	if (CrustTiles && !EditIsland.Contains(L))
	{
		CrustTiles->NotifyBrush(
			L, RadiusM, false, Volume->GetStamp(), TerrainMaterial.Get(),
			[this](const FVector& P) { return SampleDensityMeters(FVector3d(P.X, P.Y, P.Z)); },
			nullptr, false, nullptr, MaterialId);
	}
	else if (!EditIsland.IsEmpty())
	{
		const int32 SavedCreates = MaxMeshCreatesPerTick;
		MaxMeshCreatesPerTick = MeshCreatesThisTick + 6;
		RemeshIsland();
		MaxMeshCreatesPerTick = SavedCreates;
	}
	for (const FGXChunkKey& C : Brush.DirtyChunks)
	{
		FGXCrustCache::InvalidateChunk(Volume->GetStamp().GetParams(), C);
	}
	{
		static double LastBoxesAt = -1.0e9;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastBoxesAt > 0.75)
		{
			RebuildEditedPageBoxes();
			LastBoxesAt = Now;
		}
	}
	MarkPersistDirty();
	if (HorizonClipmap && !(CrustTiles && CrustTiles->HasTileAt(L)))
	{
		HorizonClipmap->NotifyBrush(
			L,
			RadiusM,
			[this](const FVector& P)
			{
				return SampleDensityMeters(FVector3d(P.X, P.Y, P.Z));
			},
			[this](const FVector& P)
			{
				return ShouldPunchClipmap(P);
			},
			false);
	}
	GX_PERF(1, TEXT("GX-place volume pages local=(%.1f,%.1f,%.1f) r=%.2f dirty=%d boxes=%d"),
		L.X, L.Y, L.Z, RadiusM, Brush.DirtyChunks.Num(), EditedPageBoxesM.Num());
	return Out;
}

int32 AGXVoxelWorld::SelectLOD(float DistanceM) const
{
	if (bForceLOD0)
	{
		return 0;
	}
	// Near field stays 1 m. Past that, stride 2 / 4 — skirts hide the crack.
	if (DistanceM <= NearFieldRadius)
	{
		return 0;
	}
	if (DistanceM <= StreamRadius)
	{
		return 1;
	}
	return 2;
}

void AGXVoxelWorld::RefreshLoadState()
{
	LastMeshedNear = 0;
	LastHollowNear = 0;
	const FVector Local = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const float NearSq = NearFieldRadius * NearFieldRadius;
	auto IsNear = [&](const FGXChunkKey& Key)
	{
		const FVector Center((Key.X + 0.5f) * ChunkM, (Key.Y + 0.5f) * ChunkM, (Key.Z + 0.5f) * ChunkM);
		return FVector::DistSquared(Center, Local) <= NearSq;
	};

	for (const auto& Pair : ChunkVisuals)
	{
		if (IsNear(Pair.Key) && Pair.Value.Bank >= 0)
		{
			++LastMeshedNear;
		}
	}
	// Air / solid-interior chunks mesh to nothing. They are finished work, not missing terrain.
	for (const FGXChunkKey& Key : HollowChunks)
	{
		if (IsNear(Key))
		{
			++LastHollowNear;
		}
	}

	const int32 Queue = NearMeshQueue.Num() + MeshQueue.Num() + AsyncInFlight.Num();
	// Desired is crust-overlapping near chunks. Settled empties count as resolved
	// so 32 meshes + 132 air does not sit on 32/164 forever.
	const int32 Desired = FMath::Max(LastDesiredNear, LastMeshedNear + LastHollowNear);
	const int32 Resolved = FMath::Min(LastMeshedNear + LastHollowNear, FMath::Max(Desired, 1));
	const float MeshFrac = (Desired > 0)
		? static_cast<float>(Resolved) / static_cast<float>(Desired)
		: 0.0f;
	const float QueueFrac = (Queue <= 0) ? 1.0f : FMath::Clamp(1.0f - Queue / 80.0f, 0.0f, 0.85f);

	if (bWorldReady)
	{
		LoadStatus = TEXT("Ready");
		LoadProgress = 1.0f;
		return;
	}

	if (!bAtlasReady)
	{
		LoadStatus = bAtlasBuildInFlight
			? TEXT("Baking crust height field…")
			: TEXT("Preparing planet…");
		LoadProgress = bAtlasBuildInFlight ? 0.08f : 0.03f;
	}
	else if (LastMeshedNear == 0 && Resolved == 0)
	{
		LoadStatus = CacheHits + CacheMisses > 0
			? TEXT("Streaming cached crust…")
			: TEXT("Generating crust density…");
		LoadProgress = 0.12f;
	}
	else if (Queue > 0 && MeshFrac < 0.95f)
	{
		LoadStatus = FString::Printf(TEXT("Meshing near-field terrain  %d / %d"), Resolved, FMath::Max(Desired, 1));
		LoadProgress = 0.14f + 0.70f * MeshFrac;
	}
	else if (Queue > 8)
	{
		LoadStatus = FString::Printf(TEXT("Streaming horizon  %d chunks queued"), Queue);
		LoadProgress = 0.80f + 0.10f * QueueFrac;
	}
	else if (WarmupTimeRemaining > 0.0f)
	{
		LoadStatus = TEXT("Cooking collision underfoot…");
		LoadProgress = 0.92f;
	}
	else
	{
		LoadStatus = TEXT("Compiling shaders / lighting…");
		LoadProgress = 0.96f;
	}

	// Clipmap is the walkable planet. Voxel shells are optional detail.
	// 0.8.16 skipped surface meshes, so LastMeshedNear stayed 0 and the
	// overlay never left "Generating crust".
	const bool bClipReady = HorizonClipmap && HorizonClipmap->IsReady();
	const bool bTilesReady = CrustTiles && CrustTiles->IsReady();
	const bool bHaveGround = bDrawVoxelVisuals
		? (LastMeshedNear >= 2)
		: (bTilesReady || bClipReady);
	if (bAtlasReady && bHaveGround && (bTilesReady || !CrustTiles.IsValid()))
	{
		if (!bWorldReady)
		{
			UE_LOG(LogGXVoxel, Warning,
				TEXT("GX-%s Ready clip=%d tiles=%d near=%d drawVox=%d"),
				GX_VERSION_STRING, bClipReady ? 1 : 0,
				CrustTiles ? CrustTiles->NumLive() : 0,
				LastMeshedNear, bDrawVoxelVisuals ? 1 : 0);
		}
		LoadStatus = TEXT("Ready");
		LoadProgress = 1.0f;
		bWorldReady = true;
		WarmupTimeRemaining = 0.0f;
	}
	GX_PERF(2, TEXT("GX-load mesh=%d hollowNear=%d desired=%d queue=%d status=%s"),
		LastMeshedNear, LastHollowNear, Desired, Queue, *LoadStatus);
}

void AGXVoxelWorld::InvalidateHollow(const FGXChunkKey& Coord)
{
	HollowChunks.Remove(Coord);
	EmptyRetries.Remove(Coord);
	NextEmptyRetryAt.Remove(Coord);
}

void AGXVoxelWorld::MarkChunkEmpty(const FGXChunkKey& Coord, int32 LOD, const TCHAR* Reason)
{
	if (LOD > 0)
	{
		// Coarse LOD often misses a 1 m crust. Retry at LOD0 — do not settle
		// a hole the player will walk into.
		GX_PERF(2, TEXT("GX-empty lod%d retry %d_%d_%d %s"), LOD, Coord.X, Coord.Y, Coord.Z,
			Reason ? Reason : TEXT("?"));
		BrushForceLOD0.Add(Coord);
		EnqueueRemesh(Coord, true);
		return;
	}

	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const FVector Local = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const float Dist = FVector::Dist(Center, Local);
	const bool bOnCrust = ChunkOverlapsSurface(Coord, ChunkM);

	// Keep an existing visual. Releasing it was "ground disappeared".
	if (ChunkVisuals.Contains(Coord))
	{
		return;
	}

	// Overlap said crust but MC produced nothing. Two retries, then stop —
	// clipmap is the continuous floor. Retrying forever was the overlay flip
	// and the climbing cache-miss count at 25/32.
	if (bOnCrust)
	{
		int32& Holds = EmptyRetries.FindOrAdd(Coord);
		if (Holds >= 2)
		{
			return;
		}
		const double Now = FPlatformTime::Seconds();
		const double* Next = NextEmptyRetryAt.Find(Coord);
		if (Next && Now < *Next)
		{
			return;
		}
		++Holds;
		NextEmptyRetryAt.Add(Coord, Now + 2.5);
		GX_PERF(2, TEXT("GX-empty crust-hold %d/%d %d_%d_%d d=%.0f %s"),
			Holds, 2, Coord.X, Coord.Y, Coord.Z, Dist, Reason ? Reason : TEXT("?"));
		BrushForceLOD0.Add(Coord);
		EnqueueRemesh(Coord, Dist <= NearFieldRadius);
		return;
	}

	int32& Retries = EmptyRetries.FindOrAdd(Coord);
	if (Retries < 1)
	{
		++Retries;
		BrushForceLOD0.Add(Coord);
		EnqueueRemesh(Coord, Dist <= NearFieldRadius);
		return;
	}

	EmptyRetries.Remove(Coord);
	NextEmptyRetryAt.Remove(Coord);
	if (HollowChunks.Contains(Coord))
	{
		return;
	}
	HollowChunks.Add(Coord);
	++LastSettledEmpty;
	if (ChunkVisuals.Contains(Coord))
	{
		ReleaseVisual(Coord);
	}
	GX_PERF(2, TEXT("GX-empty settle %d_%d_%d d=%.0f air %s"),
		Coord.X, Coord.Y, Coord.Z, Dist, Reason ? Reason : TEXT("?"));
}

bool AGXVoxelWorld::ChunkOverlapsSurface(const FGXChunkKey& Coord, float ChunkM) const
{
	const FVector Origin(Coord.X * ChunkM, Coord.Y * ChunkM, Coord.Z * ChunkM);
	float SMin = 1.0e9f;
	float SMax = -1.0e9f;
	auto Acc = [&](const FVector& P)
	{
		const float R = P.Size();
		if (R < 1.0f)
		{
			SMin = FMath::Min(SMin, -PlanetRadius);
			SMax = FMath::Max(SMax, PlanetRadius);
			return;
		}
		const FVector3f Dir(P.X / R, P.Y / R, P.Z / R);
		float Surf = PlanetRadius;
		if (CrustAtlas.IsValid())
		{
			Surf = PlanetRadius + CrustAtlas->SampleHeight(Dir);
		}
		else if (Volume)
		{
			Surf = Volume->GetStamp().SampleSurfaceRadius(Dir);
		}
		const float S = R - Surf;
		SMin = FMath::Min(SMin, S);
		SMax = FMath::Max(SMax, S);
	};
	for (int32 Zi = 0; Zi <= 1; ++Zi)
	{
		for (int32 Yi = 0; Yi <= 1; ++Yi)
		{
			for (int32 Xi = 0; Xi <= 1; ++Xi)
			{
				Acc(FVector(Origin.X + Xi * ChunkM, Origin.Y + Yi * ChunkM, Origin.Z + Zi * ChunkM));
			}
		}
	}
	Acc(Origin + FVector(ChunkM * 0.5f));
	// Face centers — high-frequency ridges can miss all 8 corners + centroid.
	Acc(Origin + FVector(ChunkM * 0.5f, ChunkM * 0.5f, 0.0f));
	Acc(Origin + FVector(ChunkM * 0.5f, ChunkM * 0.5f, ChunkM));
	Acc(Origin + FVector(ChunkM * 0.5f, 0.0f, ChunkM * 0.5f));
	Acc(Origin + FVector(ChunkM * 0.5f, ChunkM, ChunkM * 0.5f));
	Acc(Origin + FVector(0.0f, ChunkM * 0.5f, ChunkM * 0.5f));
	Acc(Origin + FVector(ChunkM, ChunkM * 0.5f, ChunkM * 0.5f));
	// Half-chunk slack so a ridge that only clips a face is still meshed.
	const float Slack = ChunkM * 0.5f;
	return !(SMax < -Slack || SMin > Slack);
}

void AGXVoxelWorld::EnqueueRemeshNeighborhood(const FGXChunkKey& Coord)
{
	static const int32 Off[7][3] = {
		{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
	};
	for (int32 I = 0; I < 7; ++I)
	{
		const FGXChunkKey N(Coord.X + Off[I][0], Coord.Y + Off[I][1], Coord.Z + Off[I][2]);
		BrushForceLOD0.Add(N);
		EnqueueRemesh(N);
	}
}

void AGXVoxelWorld::EnqueueRemesh(const FGXChunkKey& Coord, bool bNear)
{
	HollowChunks.Remove(Coord);
	const double Now = FPlatformTime::Seconds();
	if (const double* Last = LastRemeshAt.Find(Coord))
	{
		if (Now - *Last < 0.40)
		{
			return;
		}
	}
	if (AsyncInFlight.Contains(Coord))
	{
		RemeshWhenIdle.Add(Coord);
		return;
	}
	if (MeshQueued.Contains(Coord))
	{
		return;
	}
	LastRemeshAt.Add(Coord, Now);
	MeshQueued.Add(Coord);
	if (bNear)
	{
		NearMeshQueue.Add(Coord);
	}
	else
	{
		MeshQueue.Add(Coord);
	}
}

void AGXVoxelWorld::UpdateStreaming(FVector WorldViewerLocation)
{
	if (!Volume)
	{
		return;
	}
	const FVector Local = WorldToLocalMeters(WorldViewerLocation);
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const float StreamNow = FMath::Clamp(ActiveStreamRadius, 120.0f, StreamRadius);
	const int32 ChunkRadius = FMath::CeilToInt(StreamNow / ChunkM) + 1;
	const FGXChunkKey Center = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(Local.X, Local.Y, Local.Z), VoxelSize));

	int32 NearWanted = 0;
	int32 SkippedAir = 0;
	int32 DeferredFar = 0;
	TSet<FGXChunkKey> Desired;
	const bool bNearBusy = bDrawVoxelVisuals
		&& (NearMeshQueue.Num() > 0 || LastMeshedNear < 2);
	for (int32 Z = -ChunkRadius; Z <= ChunkRadius; ++Z)
	{
		for (int32 Y = -ChunkRadius; Y <= ChunkRadius; ++Y)
		{
			for (int32 X = -ChunkRadius; X <= ChunkRadius; ++X)
			{
				const FGXChunkKey CC(Center.X + X, Center.Y + Y, Center.Z + Z);
				const FVector ChunkCenter(
					(CC.X + 0.5f) * ChunkM,
					(CC.Y + 0.5f) * ChunkM,
					(CC.Z + 0.5f) * ChunkM);
				const float Dist = FVector::Dist(ChunkCenter, Local);
				if (Dist > StreamNow)
				{
					continue;
				}
				const bool bEdited = Volume->ChunkHasEdits(CC);
				if (!bEdited && !ChunkOverlapsSurface(CC, ChunkM))
				{
					++SkippedAir;
					continue;
				}
				// Unedited crust is tiles unless the edit island owns it.
				if (ChunkOverlapsSurface(CC, ChunkM) && !bDrawVoxelVisuals && !bEdited
					&& !CaveChunks.Contains(CC))
				{
					continue;
				}
				if (bEdited && ChunkOverlapsSurface(CC, ChunkM) && CrustTiles
					&& CrustTiles->HasTileAt(ChunkCenter) && !CaveChunks.Contains(CC))
				{
					continue;
				}
				// Lid-only cave chunks (filter emptied) must not remesh every tick.
				if (CaveChunks.Contains(CC) && HollowChunks.Contains(CC))
				{
					continue;
				}
				if (!bEdited && !bDrawVoxelVisuals && !CaveChunks.Contains(CC))
				{
					continue;
				}
				Desired.Add(CC);
				if (Dist <= NearFieldRadius)
				{
					++NearWanted;
				}
				if (HollowChunks.Contains(CC) && !bEdited)
				{
					continue;
				}
				if (AsyncInFlight.Contains(CC) || MeshQueued.Contains(CC))
				{
					// Already meshing or waiting on a deferred Create. Re-queueing
					// here was the 0.7.26 remesh storm (thousands of cache misses).
					continue;
				}
				// 0.7.17 deferred everything past 110 m while near was busy.
				// Walking kept near busy, so the 110–360 m band never meshed
				// and the player walked off voxels onto the clipmap.
				if (bNearBusy && Dist > StreamNow * 0.90f && !bEdited)
				{
					++DeferredFar;
					continue;
				}
				if (!ChunkVisuals.Contains(CC))
				{
					if (!bEdited && EmptyRetries.FindRef(CC) >= 2)
					{
						continue;
					}
					const double* Next = NextEmptyRetryAt.Find(CC);
					if (Next && FPlatformTime::Seconds() < *Next)
					{
						continue;
					}
					EnqueueRemesh(CC, Dist <= NearFieldRadius);
				}
				else if (!BrushForceLOD0.Contains(CC))
				{
					const int32 WantLOD = SelectLOD(Dist);
					if (ChunkVisuals.FindRef(CC).LOD != WantLOD)
					{
						EnqueueRemesh(CC, Dist <= NearFieldRadius);
					}
				}
			}
		}
	}

	TArray<FGXChunkKey> DropOnTile;
	for (const auto& Pair : ChunkVisuals)
	{
		const FVector CC(
			(Pair.Key.X + 0.5f) * ChunkM,
			(Pair.Key.Y + 0.5f) * ChunkM,
			(Pair.Key.Z + 0.5f) * ChunkM);
		if (Volume && Volume->ChunkHasEdits(Pair.Key) && ChunkOverlapsSurface(Pair.Key, ChunkM)
			&& CrustTiles && CrustTiles->HasTileAt(CC) && !CaveChunks.Contains(Pair.Key))
		{
			DropOnTile.Add(Pair.Key);
		}
	}
	for (const FGXChunkKey& K : DropOnTile)
	{
		ReleaseVisual(K);
	}

	TArray<FGXChunkKey> ToRemove;
	for (const auto& Pair : ChunkVisuals)
	{
		if (Desired.Contains(Pair.Key))
		{
			continue;
		}
		const FVector ChunkCenter(
			(Pair.Key.X + 0.5f) * ChunkM,
			(Pair.Key.Y + 0.5f) * ChunkM,
			(Pair.Key.Z + 0.5f) * ChunkM);
		if (FVector::Dist(ChunkCenter, Local) > UnloadRadius)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FGXChunkKey& K : ToRemove)
	{
		ReleaseVisual(K);
		if (AGXVoxelChunkProxy* A = ChunkActors.FindRef(K).Get())
		{
			A->Destroy();
		}
		ChunkActors.Remove(K);
	}
	LastDesiredNear = NearWanted;
	{
		static int32 PrevW = -1, PrevN = -1, PrevQ = -1, PrevI = -1, PrevH = -1;
		static double LastLog = 0.0;
		const int32 Qn = NearMeshQueue.Num();
		const int32 Qf = MeshQueue.Num();
		const int32 Inf = AsyncInFlight.Num();
		const int32 Hol = HollowChunks.Num();
		const double Now = FPlatformTime::Seconds();
		const bool bChanged = FMath::Abs(Desired.Num() - PrevW) > 40 || FMath::Abs(NearWanted - PrevN) > 8
			|| Inf != PrevI;
		if (bChanged || (Now - LastLog) >= 2.0)
		{
			PrevW = Desired.Num();
			PrevN = NearWanted;
			PrevQ = Qn + Qf;
			PrevI = Inf;
			PrevH = Hol;
			LastLog = Now;
			GX_PERF(1, TEXT("GX-stream wanted=%d near=%d skipAir=%d deferFar=%d hollow=%d settled=%d queue=%d+%d inflight=%d"),
				Desired.Num(), NearWanted, SkippedAir, DeferredFar, Hol, LastSettledEmpty,
				Qn, Qf, Inf);
		}
	}
}

void AGXVoxelWorld::FlushMeshQueue(int32 MaxBuilds)
{
	ProcessMeshQueue(FMath::Max(0, MaxBuilds));
	DrainPendingMeshes(MaxBuilds);
}

void AGXVoxelWorld::ProcessMeshQueue(int32 Budget)
{
	int32 Built = 0;
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(MeshTimeBudgetMs, 2.0f) * 0.001;
	const bool bAsync = bAsyncMeshing && Jobs.IsValid();
	auto PopNext = [this]() -> FGXChunkKey
	{
		if (NearMeshQueue.Num() > 0)
		{
			return NearMeshQueue.Pop(EAllowShrinking::No);
		}
		return MeshQueue.Pop(EAllowShrinking::No);
	};

	TSet<FGXChunkKey> HandledThisTick;
	while (Built < Budget
		&& (NearMeshQueue.Num() + MeshQueue.Num()) > 0
		&& FPlatformTime::Seconds() < Deadline)
	{
		const FGXChunkKey Coord = PopNext();
		MeshQueued.Remove(Coord);
		if (HandledThisTick.Contains(Coord))
		{
			// Empty-retry requeued the same key (LIFO). Leave it for next tick
			// so we do not burn 4 identical meshes and settle a hole this frame.
			if (!MeshQueued.Contains(Coord))
			{
				MeshQueued.Add(Coord);
				NearMeshQueue.Insert(Coord, 0);
			}
			continue;
		}
		HandledThisTick.Add(Coord);
		if (AsyncInFlight.Contains(Coord))
		{
			RemeshWhenIdle.Add(Coord);
			continue;
		}
		if (HollowChunks.Contains(Coord) && !(Volume && Volume->ChunkHasEdits(Coord)))
		{
			continue;
		}

		const FVector Local = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
		const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
		const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
		const int32 LOD = BrushForceLOD0.Contains(Coord) ? 0 : SelectLOD(FVector::Dist(Center, Local));

		if (TryApplyCachedChunk(Coord, LOD))
		{
			BrushForceLOD0.Remove(Coord);
			++CacheHits;
			++Built;
			continue;
		}
		const int32 CreateCap = (WarmupTimeRemaining > 0.0f) ? 8 : FMath::Max(1, MaxMeshCreatesPerTick);
		if (MeshCreatesThisTick >= CreateCap)
		{
			MeshQueued.Add(Coord);
			if (FVector::Dist(Center, Local) <= NearFieldRadius)
			{
				NearMeshQueue.Insert(Coord, 0);
			}
			else
			{
				MeshQueue.Insert(Coord, 0);
			}
			break;
		}
		++CacheMisses;

		const int32 InFlight = Jobs ? Jobs->NumInFlight() : AsyncInFlight.Num();
		if (!bAsync)
		{
			BuildChunkMeshSync(Coord);
		}
		else if (InFlight < MaxAsyncInFlight && AsyncInFlight.Num() < MaxAsyncInFlight)
		{
			EnqueueChunkMeshAsync(Coord);
		}
		else
		{
			// Pool is full — put the chunk back and yield this tick.
			MeshQueued.Add(Coord);
			if (FVector::Dist(Center, Local) <= NearFieldRadius)
			{
				NearMeshQueue.Add(Coord);
			}
			else
			{
				MeshQueue.Add(Coord);
			}
			break;
		}
		++Built;
	}
}

void AGXVoxelWorld::BuildChunkMeshSync(const FGXChunkKey& Coord)
{
	if (!Volume)
	{
		return;
	}
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = PublishMeshSnapshot();
	const FVector Local = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const int32 LOD = BrushForceLOD0.Contains(Coord) ? 0 : SelectLOD(FVector::Dist(Center, Local));
	BrushForceLOD0.Remove(Coord);
	FGXMesher::FSettings S;
	S.LOD = LOD;
	FGXMeshBuffers Mesh = FGXMesher::MeshChunk(*Snap, Coord, S);
	PersistChunkMesh(Coord, Mesh);
	ApplyBuiltMesh(Coord, LOD, MoveTemp(Mesh));
}

void AGXVoxelWorld::EnqueueChunkMeshAsync(const FGXChunkKey& Coord)
{
	if (!Volume || !Jobs || AsyncInFlight.Contains(Coord) || !MeshMailbox.IsValid())
	{
		return;
	}
	AsyncInFlight.Add(Coord);
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = PublishMeshSnapshot();
	const FGXGenerationStamp Stamp = Snap->Stamp;
	const FVector Local = WorldToLocalMeters(GetPrimaryInvokerLocation());
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const int32 LOD = BrushForceLOD0.Contains(Coord) ? 0 : SelectLOD(FVector::Dist(Center, Local));
	BrushForceLOD0.Remove(Coord);

	TSharedPtr<FGXMeshMailbox, ESPMode::ThreadSafe> Box = MeshMailbox;
	Jobs->Enqueue(EGXJobPriority::NearMesh, Stamp,
		[Box, Coord, LOD, Snap, Stamp]()
		{
			if (!Box.IsValid() || !Box->bAlive.Load())
			{
				return;
			}
			FGXMesher::FSettings S;
			S.LOD = LOD;
			FGXMeshBuffers Built = FGXMesher::MeshChunk(*Snap, Coord, S);
			if (!Box->bAlive.Load())
			{
				return;
			}
			FScopeLock Lock(&Box->CS);
			FGXMeshMailbox::FItem& Item = Box->Pending.AddDefaulted_GetRef();
			Item.Coord = Coord;
			Item.LOD = LOD;
			Item.Stamp = Stamp;
			Item.Mesh = MoveTemp(Built);
		});
}

void AGXVoxelWorld::DrainPendingMeshes(int32 Budget)
{
	TArray<FGXMeshMailbox::FItem> Local;
	if (MeshMailbox.IsValid())
	{
		FScopeLock Lock(&MeshMailbox->CS);
		const int32 N = FMath::Min(Budget, MeshMailbox->Pending.Num());
		for (int32 I = 0; I < N; ++I)
		{
			Local.Add(MoveTemp(MeshMailbox->Pending[I]));
		}
		MeshMailbox->Pending.RemoveAt(0, N, EAllowShrinking::No);
	}
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(MeshTimeBudgetMs, 2.0f) * 0.001;
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		FGXMeshMailbox::FItem& P = Local[I];
		if (I > 0 && FPlatformTime::Seconds() >= Deadline)
		{
			// Put leftover applies back — do not spend 81 ms * 6 in one tick.
			if (MeshMailbox.IsValid())
			{
				FScopeLock Lock(&MeshMailbox->CS);
				for (int32 J = Local.Num() - 1; J >= I; --J)
				{
					MeshMailbox->Pending.Insert(MoveTemp(Local[J]), 0);
				}
			}
			break;
		}
		AsyncInFlight.Remove(P.Coord);
		if (RemeshWhenIdle.Remove(P.Coord))
		{
			EnqueueRemesh(P.Coord, true);
		}
		if (Jobs && !Jobs->ShouldApply(P.Stamp))
		{
			continue;
		}
		PersistChunkMesh(P.Coord, P.Mesh);
		if (!ApplyBuiltMesh(P.Coord, P.LOD, MoveTemp(P.Mesh)))
		{
			// Create budget hit — mesh is back in the mailbox. Stop this tick.
			if (MeshMailbox.IsValid())
			{
				FScopeLock Lock(&MeshMailbox->CS);
				for (int32 J = Local.Num() - 1; J > I; --J)
				{
					MeshMailbox->Pending.Insert(MoveTemp(Local[J]), 0);
				}
			}
			break;
		}
	}
}

void AGXVoxelWorld::DeferMeshApply(const FGXChunkKey& Coord, int32 LOD, FGXMeshBuffers&& MeshData)
{
	if (!MeshMailbox.IsValid() || MeshData.IsEmpty())
	{
		return;
	}
	FScopeLock Lock(&MeshMailbox->CS);
	FGXMeshMailbox::FItem& Item = MeshMailbox->Pending.AddDefaulted_GetRef();
	Item.Coord = Coord;
	Item.LOD = LOD;
	Item.Mesh = MoveTemp(MeshData);
}

bool AGXVoxelWorld::ApplyBuiltMesh(const FGXChunkKey& Coord, int32 LOD, FGXMeshBuffers&& MeshData)
{
	if (CaveChunks.Contains(Coord))
	{
		const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
		const FVector C((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
		const FBox Box(C - FVector(ChunkM * 0.5f), C + FVector(ChunkM * 0.5f));
		if (!EditIsland.OverlapsBox(Box))
		{
			const int32 Before = MeshData.Indices.Num();
			FilterMeshToCarveBalls(Coord, MeshData);
			GX_PERF(1, TEXT("GX-cave filter %d_%d_%d tris %d -> %d"),
				Coord.X, Coord.Y, Coord.Z, Before / 3, MeshData.Indices.Num() / 3);
			if (MeshData.IsEmpty())
			{
				HollowChunks.Add(Coord);
				return true;
			}
		}
	}
	if (MeshData.IsEmpty())
	{
		MarkChunkEmpty(Coord, LOD, TEXT("mesh"));
		return true;
	}
	HollowChunks.Remove(Coord);
	EmptyRetries.Remove(Coord);
	NextEmptyRetryAt.Remove(Coord);
	EnsureMeshBanks();

	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const FVector ViewerLocal = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const bool bHadVisual = ChunkVisuals.Contains(Coord);

	int32 Bank = INDEX_NONE;
	int32 Section = INDEX_NONE;
	if (!AcquireVisual(Coord, Bank, Section))
	{
		if (!EvictFurthestVisual(ViewerLocal, ChunkM))
		{
			GrowMeshBanks(MeshBanks.Num() + 4);
		}
		if (!AcquireVisual(Coord, Bank, Section))
		{
			EvictFurthestVisual(ViewerLocal, ChunkM);
			if (!AcquireVisual(Coord, Bank, Section))
			{
				UE_LOG(LogGXVoxel, Warning, TEXT("GX-visual bank full, drop %d_%d_%d banks=%d free=%d vis=%d"),
					Coord.X, Coord.Y, Coord.Z, MeshBanks.Num(), FreeVisualSlots.Num(), ChunkVisuals.Num());
				return true;
			}
		}
	}

	UProceduralMeshComponent* PMC = MeshBanks.IsValidIndex(Bank) ? MeshBanks[Bank].Get() : nullptr;
	if (!PMC)
	{
		ReleaseVisual(Coord);
		return true;
	}

	TArray<FVector> LocalPos;
	TArray<FProcMeshTangent> Tangents;
	LocalPos.Reserve(MeshData.Positions.Num());
	Tangents.Reserve(MeshData.Positions.Num());
	for (int32 I = 0; I < MeshData.Positions.Num(); ++I)
	{
		// Banks live on the planet actor (origin). Verts are planet-local cm.
		// Subtracting OriginM put the whole crust in the core (0.7.21 void).
		LocalPos.Add(MeshData.Positions[I] * GMetersToUU);
		FVector T = FVector::CrossProduct(
			MeshData.Normals.IsValidIndex(I) ? MeshData.Normals[I] : FVector::UpVector,
			FVector::UpVector);
		if (T.SizeSquared() < 1e-6f)
		{
			T = FVector::RightVector;
		}
		T.Normalize();
		Tangents.Add(FProcMeshTangent(T, false));
	}

	const bool bEdited = Volume && Volume->ChunkHasEdits(Coord);
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const bool bNearCol = FVector::Dist(Center, ViewerLocal) <= CollisionRadius;
	const bool bCookCol = (bEdited && bNearCol) || CaveChunks.Contains(Coord);

	PMC->bUseAsyncCooking = !bCookCol;
	FChunkVisual* Slot = ChunkVisuals.Find(Coord);
	const bool bCanUpdate = !bEdited && Slot
		&& Slot->VertCount == LocalPos.Num()
		&& Slot->IndexCount == MeshData.Indices.Num()
		&& Slot->VertCount > 0;
	if (bCanUpdate)
	{
		PMC->UpdateMeshSection_LinearColor(
			Section, LocalPos, MeshData.Normals, MeshData.UV0, MeshData.Colors, Tangents);
	}
	else
	{
		// 48-section PMC Create rebuilt the whole proxy (~81 ms). Cap Creates
		// so walking stays interactive; leftover meshes wait in the mailbox.
		const int32 CreateCap = (WarmupTimeRemaining > 0.0f) ? 8 : FMath::Max(1, MaxMeshCreatesPerTick);
		if (MeshCreatesThisTick >= CreateCap)
		{
			if (!bHadVisual)
			{
				ReleaseVisual(Coord);
			}
			MeshQueued.Add(Coord);
			DeferMeshApply(Coord, LOD, MoveTemp(MeshData));
			return false;
		}
		PMC->ClearMeshSection(Section);
		PMC->CreateMeshSection_LinearColor(
			Section, LocalPos, MeshData.Indices, MeshData.Normals, MeshData.UV0, MeshData.Colors, Tangents, bCookCol);
		++MeshCreatesThisTick;
	}
	if (TerrainMaterial)
	{
		PMC->SetMaterial(Section, TerrainMaterial);
	}
	PMC->SetMeshSectionVisible(Section, true);
	if (bEdited)
	{
		// Do not enable shadows on the bank PMC. Verts live 60 km from the
		// component, so VSM treats one section as covering the whole map
		// ([VSM] Non-Nanite Marking Job Queue overflow, 0.8.10).
		PMC->SetCastShadow(false);
		PMC->bCastDynamicShadow = false;
		PMC->SetVisibleInRayTracing(false);
		PMC->bAffectDistanceFieldLighting = false;
		// Do not NotifyEdits here. Each first visual rescanned the whole
		// 80 m disk (400 ms, 0.8.22 spawn/dig hitch). The click already
		// opened the lid; load-time NotifyEdits is enough.
	}
	if (bCookCol)
	{
		PMC->bUseComplexAsSimpleCollision = true;
		PMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PMC->SetCollisionObjectType(ECC_WorldStatic);
		PMC->SetCollisionResponseToAllChannels(ECR_Block);
	}
	if (FChunkVisual* V = ChunkVisuals.Find(Coord))
	{
		V->LOD = LOD;
		V->VertCount = LocalPos.Num();
		V->IndexCount = MeshData.Indices.Num();
	}

	MeshQueued.Remove(Coord);
	return true;
}

bool AGXVoxelWorld::PlacePawnOnSurface(APawn* Pawn, FVector RadialHint)
{
	if (!Pawn || !Volume)
	{
		return false;
	}

	// Always sit on the stamp crust. "Already on floor" used density, which
	// is a cave under saved air — you spawned under the 0.9 tiles (192046).
	const FVector Surface = FindSurfaceWorldLocation(RadialHint);
	const FVector Up = -GetGravityDirectionAt(Surface);
	float Half = 88.0f;
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UCapsuleComponent* Cap = Char->GetCapsuleComponent())
		{
			Half = Cap->GetScaledCapsuleHalfHeight();
		}
	}
	const FVector SpawnLoc = Surface + Up * (Half + 4.0f);
	CachedViewerWorld = SpawnLoc;
	LastStreamViewerWorld = FVector(1e12f, 0, 0);
	ActiveStreamRadius = FMath::Max(ActiveStreamRadius, 140.0f);
	EnsureCrustAtlas();
	if (bAtlasReady && bDrawVoxelVisuals)
	{
		UpdateStreaming(SpawnLoc);
		FlushMeshQueue(8);
	}

	Pawn->SetActorLocation(SpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
	FVector Forward = FVector::VectorPlaneProject(FVector(0, 1, 0), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(FVector(0, 0, 1), Up).GetSafeNormal();
	}
	Pawn->SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UGXBodyMovement* Move = Cast<UGXBodyMovement>(Char->GetCharacterMovement()))
		{
			Move->TryFindField();
			// Stamp crust only. SnapToSurface uses density — a saved cave
			// under a closed lid yanks the pawn underground (0.11.0 reload).
			Move->NotifyJustSpawned();
		}
		else if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->SetGravityDirection(GetGravityDirectionAt(Pawn->GetActorLocation()));
			CMC->SetMovementMode(MOVE_Walking);
		}
	}

	CachedViewerWorld = Pawn->GetActorLocation();
	if (bAtlasReady)
	{
		UpdateStreaming(CachedViewerWorld);
	}

	UE_LOG(LogGXVoxel, Warning,
		TEXT("GX-%s PlacePawnOnSurface r=%.1fm want=%.1fm loc=%s"),
		GX_VERSION_STRING,
		WorldToLocalMeters(Pawn->GetActorLocation()).Size(),
		PlanetRadius,
		*Pawn->GetActorLocation().ToCompactString());
	return true;
}

TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> AGXVoxelWorld::PublishMeshSnapshot() const
{
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume
		? Volume->PublishSnapshot()
		: MakeShared<FGXVoxelSnapshot, ESPMode::ThreadSafe>();
	Snap->Atlas = CrustAtlas;
	if (Jobs)
	{
		Snap->Stamp = Jobs->GetStamp();
	}
	return Snap;
}

void AGXVoxelWorld::EnsureCrustAtlas()
{
	if (bAtlasReady || bAtlasBuildInFlight || !Volume)
	{
		return;
	}
	const FGXPlanetStampParams Params = Volume->GetStamp().GetParams();
	if (TSharedPtr<FGXCrustAtlas, ESPMode::ThreadSafe> Loaded = FGXCrustAtlas::LoadFromFile(FGXCrustCache::AtlasPath(Params)))
	{
		OnAtlasReady(Loaded.ToSharedRef(), true);
		return;
	}

	bAtlasBuildInFlight = true;
	LoadStatus = TEXT("Baking crust height field…");
	LoadProgress = 0.06f;
	FVector Dir = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero()
		? LocalMetersToWorld(FVector(PlanetRadius, 0, 0))
		: CachedViewerWorld).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector(1, 0, 0);
	}
	const float HalfExtent = StreamRadius + 48.0f;
	TWeakObjectPtr<AGXVoxelWorld> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [Params, Dir, HalfExtent, WeakThis]()
	{
		TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe> Built =
			FGXCrustAtlas::Build(Params, Dir, HalfExtent, 2.5f);
		Built->SaveToFile(FGXCrustCache::AtlasPath(Params));
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Built]()
		{
			if (AGXVoxelWorld* World = WeakThis.Get())
			{
				World->OnAtlasReady(Built, false);
			}
		});
	});
}

void AGXVoxelWorld::OnAtlasReady(const TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe>& Built, bool bFromDisk)
{
	CrustAtlas = Built;
	bAtlasReady = true;
	bAtlasBuildInFlight = false;
	LoadStatus = bFromDisk ? TEXT("Loaded crust cache…") : TEXT("Height field ready…");
	LoadProgress = 0.12f;
	LastStreamViewerWorld = FVector(1e12f, 0, 0);
	if (Volume)
	{
		TArray<FGXChunkKey> Edited;
		Volume->GetAllocatedChunkKeys(Edited);
		for (const FGXChunkKey& K : Edited)
		{
			if (Volume->ChunkHasEdits(K))
			{
				BrushForceLOD0.Add(K);
				EnqueueRemesh(K, true);
			}
		}
		RebuildEditedPageBoxes();
		if (HorizonClipmap)
		{
			HorizonClipmap->NotifyEdits();
		}
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s crust atlas ready disk=%d dim=%d editBoxes=%d"),
		GX_VERSION_STRING, bFromDisk ? 1 : 0, Built->Dim, EditedPageBoxesM.Num());
}

bool AGXVoxelWorld::TryApplyCachedChunk(const FGXChunkKey& Coord, int32 LOD)
{
	if (!Volume || Volume->ChunkHasEdits(Coord))
	{
		return false;
	}
	const FString Path = FGXCrustCache::ChunkPath(Volume->GetStamp().GetParams(), Coord);
	int32 FileLOD = LOD;
	FGXMeshBuffers Mesh;
	if (!FGXCrustCache::LoadMesh(Path, FileLOD, Mesh))
	{
		return false;
	}
	if (Mesh.IsEmpty())
	{
		// Stale empty files are not proof. Remesh at LOD0 instead of settling.
		IFileManager::Get().Delete(*Path, false, true, true);
		GX_PERF(2, TEXT("GX-cache empty-delete %d_%d_%d"), Coord.X, Coord.Y, Coord.Z);
		return false;
	}
	ApplyBuiltMesh(Coord, FileLOD, MoveTemp(Mesh));
	return true;
}

void AGXVoxelWorld::EnsureMeshBanks()
{
	GrowMeshBanks(VisualBankCount);
}

void AGXVoxelWorld::GrowMeshBanks(int32 TargetBanks)
{
	TargetBanks = FMath::Clamp(TargetBanks, VisualBankCount, VisualBankMax);
	if (MeshBanks.Num() >= TargetBanks)
	{
		return;
	}
	USceneComponent* Root = GetRootComponent();
	const int32 From = MeshBanks.Num();
	for (int32 B = From; B < TargetBanks; ++B)
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this);
		if (!PMC)
		{
			continue;
		}
		PMC->SetupAttachment(Root);
		PMC->RegisterComponent();
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->bUseAsyncCooking = true;
		PMC->SetCastShadow(false);
		PMC->SetVisibleInRayTracing(false);
		PMC->bNeverDistanceCull = true;
		PMC->SetBoundsScale(1.0f);
		MeshBanks.Add(PMC);
		for (int32 S = 0; S < VisualSectionsPerBank; ++S)
		{
			FreeVisualSlots.Add(B * VisualSectionsPerBank + S);
		}
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s visual banks=%d slots=%d (grew from %d)"),
		GX_VERSION_STRING, MeshBanks.Num(), FreeVisualSlots.Num() + ChunkVisuals.Num(), From);
}

bool AGXVoxelWorld::AcquireVisual(const FGXChunkKey& Key, int32& OutBank, int32& OutSection)
{
	if (const FChunkVisual* Have = ChunkVisuals.Find(Key))
	{
		OutBank = Have->Bank;
		OutSection = Have->Section;
		return OutBank >= 0 && OutSection >= 0;
	}
	if (FreeVisualSlots.Num() == 0)
	{
		return false;
	}
	const int32 Packed = FreeVisualSlots.Pop(EAllowShrinking::No);
	OutBank = Packed / VisualSectionsPerBank;
	OutSection = Packed % VisualSectionsPerBank;
	FChunkVisual V;
	V.Bank = OutBank;
	V.Section = OutSection;
	ChunkVisuals.Add(Key, V);
	return true;
}

void AGXVoxelWorld::ReleaseVisual(const FGXChunkKey& Key)
{
	FChunkVisual Slot;
	if (!ChunkVisuals.RemoveAndCopyValue(Key, Slot))
	{
		return;
	}
	if (MeshBanks.IsValidIndex(Slot.Bank) && MeshBanks[Slot.Bank])
	{
		MeshBanks[Slot.Bank]->ClearMeshSection(Slot.Section);
	}
	if (Slot.Bank >= 0 && Slot.Section >= 0)
	{
		FreeVisualSlots.Add(Slot.Bank * VisualSectionsPerBank + Slot.Section);
	}
}

bool AGXVoxelWorld::EvictFurthestVisual(const FVector& ViewerLocalM, float ChunkM)
{
	// Never evict inside the unload sphere — that punched black hillside holes
	// when 16×48 slots filled with still-streamed crust.
	const float KeepSq = FMath::Square(FMath::Max(UnloadRadius, StreamRadius + 40.0f));
	float BestDs = KeepSq;
	FGXChunkKey Best = FGXChunkKey();
	bool bFound = false;
	for (const auto& Pair : ChunkVisuals)
	{
		const FVector C((Pair.Key.X + 0.5f) * ChunkM, (Pair.Key.Y + 0.5f) * ChunkM, (Pair.Key.Z + 0.5f) * ChunkM);
		const float Ds = FVector::DistSquared(C, ViewerLocalM);
		if (Ds > BestDs)
		{
			BestDs = Ds;
			Best = Pair.Key;
			bFound = true;
		}
	}
	if (bFound)
	{
		ReleaseVisual(Best);
	}
	return bFound;
}

void AGXVoxelWorld::PersistChunkMesh(const FGXChunkKey& Coord, const FGXMeshBuffers& Mesh) const
{
	if (!Volume || Volume->ChunkHasEdits(Coord) || Mesh.IsEmpty())
	{
		return;
	}
	const FString Path = FGXCrustCache::ChunkPath(Volume->GetStamp().GetParams(), Coord);
	FGXMeshBuffers Copy = Mesh;
	Async(EAsyncExecution::ThreadPool, [Path, Copy = MoveTemp(Copy)]()
	{
		FGXCrustCache::SaveMesh(Path, 0, Copy);
	});
}

FString AGXVoxelWorld::GetSavePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelWorld"), SaveFileName);
}

bool AGXVoxelWorld::SaveWorld()
{
	if (!Volume)
	{
		return false;
	}
	TArray<uint8> Buf;
	auto Write = [&](const void* Data, int32 Size)
	{
		const int32 Off = Buf.Num();
		Buf.AddUninitialized(Size);
		FMemory::Memcpy(Buf.GetData() + Off, Data, Size);
	};
	uint32 Mag = GXPersist::Magic, Ver = GXPersist::Version;
	Write(&Mag, 4); Write(&Ver, 4);
	Write(&Seed, 4); Write(&PlanetRadius, 4); Write(&MaxRelief, 4); Write(&VoxelSize, 4);
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume->PublishSnapshot();
	int32 PageCount = 0;
	for (const auto& Pair : Snap->Pages)
	{
		for (const auto& P : Pair.Value)
		{
			if (P.IsValid()) ++PageCount;
		}
	}
	Write(&PageCount, 4);
	for (const auto& Pair : Snap->Pages)
	{
		for (int32 I = 0; I < Pair.Value.Num(); ++I)
		{
			if (!Pair.Value[I].IsValid()) continue;
			Write(&Pair.Key.X, 4); Write(&Pair.Key.Y, 4); Write(&Pair.Key.Z, 4);
			Write(&I, 4);
			Write(Pair.Value[I]->Cells.GetData(), sizeof(FGXVoxelPacked) * FGXVoxelConstants::CellsPerPage);
		}
	}
	int32 IslandN = EditIsland.Spheres.Num();
	Write(&IslandN, 4);
	for (const FGXEditSphere& S : EditIsland.Spheres)
	{
		const float X = static_cast<float>(S.C.X);
		const float Y = static_cast<float>(S.C.Y);
		const float Z = static_cast<float>(S.C.Z);
		const float R = S.R;
		Write(&X, 4); Write(&Y, 4); Write(&Z, 4); Write(&R, 4);
	}
	const FString Path = GetSavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(Buf, *Path))
	{
		UE_LOG(LogGXVoxel, Error, TEXT("GX-%s SaveWorld failed %s"), GX_VERSION_STRING, *Path);
		return false;
	}
	LastSaveToast = FString::Printf(TEXT("Saved %s"), *FDateTime::Now().ToString(TEXT("%H:%M")));
	bPersistDirty = false;
	AutoSaveAccum = 0.0f;
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s saved %d pages island=%s -> %s (%s)"),
		GX_VERSION_STRING, PageCount, *IslandDebugString(), *Path, *LastSaveToast);
	GX_PERF(1, TEXT("GX-save pages=%d island=%s path=%s"), PageCount, *IslandDebugString(), *Path);
	return true;
}

bool AGXVoxelWorld::LoadWorld()
{
	if (!Volume)
	{
		RebuildParams();
	}
	TArray<uint8> Buf;
	if (!FFileHelper::LoadFileToArray(Buf, *GetSavePath()) || Buf.Num() < 28)
	{
		return false;
	}
	int32 Off = 0;
	auto Read = [&](void* Dst, int32 Size) -> bool
	{
		if (Off + Size > Buf.Num()) return false;
		FMemory::Memcpy(Dst, Buf.GetData() + Off, Size);
		Off += Size;
		return true;
	};
	uint32 Mag = 0, Ver = 0;
	if (!Read(&Mag, 4) || !Read(&Ver, 4) || Mag != GXPersist::Magic)
	{
		return false;
	}
	int32 FileSeed = 0;
	float R = 0, Rel = 0, VS = 0;
	Read(&FileSeed, 4); Read(&R, 4); Read(&Rel, 4); Read(&VS, 4);
	if (FMath::Abs(R - PlanetRadius) > FMath::Max(PlanetRadius * 0.05f, 50.0f))
	{
		UE_LOG(LogGXVoxel, Warning,
			TEXT("Skipping save %s (file R=%.0f current R=%.0f) — old 4 km pages would punch the new crust"),
			*GetSavePath(), R, PlanetRadius);
		return false;
	}
	int32 PageCount = 0;
	if (!Read(&PageCount, 4))
	{
		return false;
	}
	for (int32 P = 0; P < PageCount; ++P)
	{
		FGXChunkKey Key;
		int32 PageIndex = 0;
		if (!Read(&Key.X, 4) || !Read(&Key.Y, 4) || !Read(&Key.Z, 4) || !Read(&PageIndex, 4))
		{
			break;
		}
		FGXVoxelPacked Cells[FGXVoxelConstants::CellsPerPage];
		if (!Read(Cells, sizeof(Cells)))
		{
			break;
		}
		const int32 PX = PageIndex % FGXVoxelConstants::PagesPerAxis;
		const int32 PY = (PageIndex / FGXVoxelConstants::PagesPerAxis) % FGXVoxelConstants::PagesPerAxis;
		const int32 PZ = PageIndex / (FGXVoxelConstants::PagesPerAxis * FGXVoxelConstants::PagesPerAxis);
		for (int32 Z = 0; Z < FGXVoxelConstants::PageSize; ++Z)
		{
			for (int32 Y = 0; Y < FGXVoxelConstants::PageSize; ++Y)
			{
				for (int32 X = 0; X < FGXVoxelConstants::PageSize; ++X)
				{
					const int32 LI = FGXVoxelPage::Index(X, Y, Z);
					if (!Cells[LI].IsAuthoritative())
					{
						continue;
					}
					const FIntVector VC(
						Key.X * FGXVoxelConstants::ChunkSize + PX * FGXVoxelConstants::PageSize + X,
						Key.Y * FGXVoxelConstants::ChunkSize + PY * FGXVoxelConstants::PageSize + Y,
						Key.Z * FGXVoxelConstants::ChunkSize + PZ * FGXVoxelConstants::PageSize + Z);
					Volume->SetVoxel(VC, Cells[LI]);
				}
			}
		}
		EnqueueRemesh(Key);
		const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
		if (ChunkOverlapsSurface(Key, ChunkM))
		{
			CaveChunks.Add(Key);
		}
	}
	EditIsland.Reset();
	if (Ver >= 2)
	{
		int32 IslandN = 0;
		if (Read(&IslandN, 4))
		{
			IslandN = FMath::Clamp(IslandN, 0, FGXEditIsland::MaxSpheres);
			for (int32 I = 0; I < IslandN; ++I)
			{
				float X = 0.f, Y = 0.f, Z = 0.f, Rad = 0.f;
				if (!Read(&X, 4) || !Read(&Y, 4) || !Read(&Z, 4) || !Read(&Rad, 4))
				{
					break;
				}
				if (Ver >= 3)
				{
					FGXEditSphere S;
					S.C = FVector(X, Y, Z);
					S.R = Rad;
					EditIsland.Spheres.Add(S);
				}
			}
		}
	}
	RebuildEditedPageBoxes();
	if (!EditIsland.LooksValid(PlanetRadius, MaxRelief) && PageCount > 0)
	{
		UE_LOG(LogGXVoxel, Warning,
			TEXT("GX-%s load island invalid (ver=%u n=%d) — rebuild from air cells"),
			GX_VERSION_STRING, Ver, EditIsland.Spheres.Num());
		ReconstructIslandFromEdits();
	}
	if (HorizonClipmap)
	{
		HorizonClipmap->NotifyEdits();
	}
	bPersistDirty = false;
	bLoadRestorePending = PageCount > 0 || !EditIsland.IsEmpty();
	if (!bLoadRestorePending)
	{
		bRevealedTileEdits = true;
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s loaded %d dirty pages from %s boxes=%d island=%s ver=%u"),
		GX_VERSION_STRING, PageCount, *GetSavePath(), EditedPageBoxesM.Num(),
		*IslandDebugString(), Ver);
	GX_PERF(1, TEXT("GX-load pages=%d boxes=%d island=%s ver=%u"),
		PageCount, EditedPageBoxesM.Num(), *IslandDebugString(), Ver);
	return true;
}

FString AGXVoxelWorld::IslandDebugString() const
{
	if (EditIsland.IsEmpty())
	{
		return TEXT("n=0");
	}
	const FGXEditSphere& S = EditIsland.Spheres[0];
	return FString::Printf(TEXT("n=%d r0=%.2f |c0|=%.1f"),
		EditIsland.Spheres.Num(), S.R, S.C.Size());
}

void AGXVoxelWorld::ReconstructIslandFromEdits()
{
	EditIsland.Reset();
	if (!Volume)
	{
		return;
	}
	const float VS = VoxelSize;
	const FGXSphereStamp& Stamp = Volume->GetStamp();
	int32 AirN = 0;
	Volume->ForEachAuthoritativeCell([&](const FIntVector& VC, const FGXVoxelPacked& Cell)
	{
		if (Cell.IsSolid())
		{
			return;
		}
		const FVector P(
			(VC.X + 0.5f) * VS,
			(VC.Y + 0.5f) * VS,
			(VC.Z + 0.5f) * VS);
		const FVector Dir = P.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			return;
		}
		const float Surf = Stamp.SampleSurfaceRadius(FVector3f(Dir.X, Dir.Y, Dir.Z));
		if (P.Size() < Surf - 8.0f)
		{
			return;
		}
		EditIsland.Add(P, VS * 1.5f + FGXEditIsland::CollarM);
		++AirN;
	});
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s rebuild-island air=%d %s"),
		GX_VERSION_STRING, AirN, *IslandDebugString());
	GX_PERF(1, TEXT("GX-rebuild-island air=%d %s"), AirN, *IslandDebugString());
}

void AGXVoxelWorld::RestoreEditedSurfaces()
{
	if (!CrustTiles || !CrustTiles->IsReady())
	{
		return;
	}
	if (!EditIsland.LooksValid(PlanetRadius, MaxRelief) && Volume && Volume->GetAllocatedPageCount() > 0)
	{
		ReconstructIslandFromEdits();
	}
	if (EditIsland.IsEmpty())
	{
		bLoadRestorePending = false;
		bRevealedTileEdits = true;
		return;
	}
	const FBox IB = EditIsland.Bounds();
	if (!IB.IsValid)
	{
		return;
	}
	bool bTile = CrustTiles->HasTileAt(IB.GetCenter());
	if (!bTile)
	{
		for (const FGXEditSphere& S : EditIsland.Spheres)
		{
			if (CrustTiles->HasTileAt(S.C))
			{
				bTile = true;
				break;
			}
		}
	}
	if (!bTile)
	{
		return;
	}
	const int32 SavedCreates = MaxMeshCreatesPerTick;
	MaxMeshCreatesPerTick = MeshCreatesThisTick + 8;
	RemeshIsland();
	int32 Hidden = CrustTiles->ConsumeWhere(
		IB.GetCenter(), FMath::Max(IB.GetExtent().GetMax(), 4.0f),
		[this](const FVector& P) { return EditIsland.Contains(P); },
		TerrainMaterial.Get());
	MaxMeshCreatesPerTick = SavedCreates;
	bLoadRestorePending = false;
	bRevealedTileEdits = true;
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s restore-island consume=%d %s"),
		GX_VERSION_STRING, Hidden, *IslandDebugString());
	GX_PERF(1, TEXT("GX-restore-island consume=%d %s"), Hidden, *IslandDebugString());
}

void AGXVoxelWorld::RebuildEditedPageBoxes()
{
	EditedPageBoxesM.Reset();
	if (!Volume)
	{
		return;
	}
	Volume->GetEditedPageBoxes(EditedPageBoxesM, FMath::Max(1.5f, VoxelSize * 1.5f));
	const FGXSphereStamp& Stamp = Volume->GetStamp();
	const float R0 = Stamp.GetParams().Radius;
	for (FBox& B : EditedPageBoxesM)
	{
		const FVector C = B.GetCenter();
		FVector Dir = C.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			continue;
		}
		const FGXEarthField F = Stamp.SampleEarthField(FVector3f(Dir.X, Dir.Y, Dir.Z), false);
		const float Surf = R0 + F.HeightM;
		// Only lift a pillar to the grass when the page is actually under it.
		if (C.Size() + 3.0f < Surf)
		{
			B += Dir * (Surf + 1.5f);
		}
	}
}

bool AGXVoxelWorld::LocalInEditedPage(const FVector& LocalM) const
{
	for (const FBox& B : EditedPageBoxesM)
	{
		if (B.IsInsideOrOn(LocalM))
		{
			return true;
		}
	}
	return false;
}

void AGXVoxelWorld::RemeshAroundLocal(const FVector& LocalM, float RadiusM)
{
	if (!Volume)
	{
		return;
	}
	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const float Cover = FMath::Max(RadiusM, 1.0f) + VoxelSize * 2.0f;
	const int32 Reach = FMath::CeilToInt(Cover / ChunkM) + 1;
	const FGXChunkKey Center = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(LocalM.X, LocalM.Y, LocalM.Z), VoxelSize));
	int32 N = 0;
	for (int32 Z = -Reach; Z <= Reach; ++Z)
	{
		for (int32 Y = -Reach; Y <= Reach; ++Y)
		{
			for (int32 X = -Reach; X <= Reach; ++X)
			{
				const FGXChunkKey CC(Center.X + X, Center.Y + Y, Center.Z + Z);
				const FVector C((CC.X + 0.5f) * ChunkM, (CC.Y + 0.5f) * ChunkM, (CC.Z + 0.5f) * ChunkM);
				if (FVector::DistSquared(C, LocalM) > FMath::Square(Cover + ChunkM))
				{
					continue;
				}
				if (!Volume->ChunkHasEdits(CC) && !ChunkOverlapsSurface(CC, ChunkM))
				{
					continue;
				}
				BrushForceLOD0.Add(CC);
				EnqueueRemesh(CC, true);
				++N;
			}
		}
	}
	GX_PERF(1, TEXT("GX-remesh-footprint n=%d cover=%.0f"), N, Cover);
}

void AGXVoxelWorld::FilterMeshToCarveBalls(const FGXChunkKey& Coord, FGXMeshBuffers& Mesh) const
{
	const TArray<FVector4>* Balls = CarveBalls.Find(Coord);
	if (!Balls || Balls->Num() == 0 || Mesh.IsEmpty())
	{
		return;
	}
	TArray<FVector> AliveLid;
	if (CrustTiles)
	{
		const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
		const FVector Center(
			(Coord.X + 0.5f) * ChunkM,
			(Coord.Y + 0.5f) * ChunkM,
			(Coord.Z + 0.5f) * ChunkM);
		CrustTiles->CollectAliveQuadCentroidsNear(Center, ChunkM * 0.75f, AliveLid);
	}
	const float LidPad2 = 0.50f * 0.50f;
	TArray<int32> Kept;
	Kept.Reserve(Mesh.Indices.Num());
	auto OnAliveLid = [&AliveLid, LidPad2](const FVector& P) -> bool
	{
		for (const FVector& C : AliveLid)
		{
			if (FVector::DistSquared(P, C) <= LidPad2)
			{
				return true;
			}
		}
		return false;
	};
	for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
	{
		const int32 IA = Mesh.Indices[T];
		const int32 IB = Mesh.Indices[T + 1];
		const int32 IC = Mesh.Indices[T + 2];
		if (!Mesh.Positions.IsValidIndex(IA) || !Mesh.Positions.IsValidIndex(IB)
			|| !Mesh.Positions.IsValidIndex(IC))
		{
			continue;
		}
		const FVector Cent = (Mesh.Positions[IA] + Mesh.Positions[IB] + Mesh.Positions[IC]) * (1.0f / 3.0f);
		// Keep the whole excavated MC except floor lid sheets. Requiring
		// Inside(carve ball) dropped 1990→0 tris so punch windows showed
		// the orange ball (GX-shot-0133).
		FVector FaceN = FVector::CrossProduct(
			Mesh.Positions[IB] - Mesh.Positions[IA],
			Mesh.Positions[IC] - Mesh.Positions[IA]);
		const FVector Rad = Cent.GetSafeNormal();
		const bool bFloorLike = !FaceN.IsNearlyZero() && !Rad.IsNearlyZero()
			&& FMath::Abs(FVector::DotProduct(FaceN.GetSafeNormal(), Rad)) > 0.55f;
		if (bFloorLike && OnAliveLid(Cent))
		{
			continue;
		}
		Kept.Add(IA);
		Kept.Add(IB);
		Kept.Add(IC);
	}
	Mesh.Indices = MoveTemp(Kept);
	if (Mesh.Indices.Num() < 3)
	{
		Mesh.Reset();
	}
}

void AGXVoxelWorld::RemeshIsland()
{
	if (!Volume || EditIsland.IsEmpty())
	{
		return;
	}
	const FBox IB = EditIsland.Bounds().ExpandBy(VoxelSize * 2.0f);
	if (!IB.IsValid)
	{
		return;
	}
	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const FGXChunkKey A = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(IB.Min.X, IB.Min.Y, IB.Min.Z), VoxelSize));
	const FGXChunkKey B = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(IB.Max.X, IB.Max.Y, IB.Max.Z), VoxelSize));
	const int32 X0 = FMath::Min(A.X, B.X);
	const int32 X1 = FMath::Max(A.X, B.X);
	const int32 Y0 = FMath::Min(A.Y, B.Y);
	const int32 Y1 = FMath::Max(A.Y, B.Y);
	const int32 Z0 = FMath::Min(A.Z, B.Z);
	const int32 Z1 = FMath::Max(A.Z, B.Z);
	int32 N = 0;
	const double Now = FPlatformTime::Seconds();
	for (int32 Z = Z0; Z <= Z1 && N < 8; ++Z)
	{
		for (int32 Y = Y0; Y <= Y1 && N < 8; ++Y)
		{
			for (int32 X = X0; X <= X1 && N < 8; ++X)
			{
				const FGXChunkKey CC(X, Y, Z);
				const FVector C((X + 0.5f) * ChunkM, (Y + 0.5f) * ChunkM, (Z + 0.5f) * ChunkM);
				const FBox Box(C - FVector(ChunkM * 0.5f), C + FVector(ChunkM * 0.5f));
				if (!EditIsland.OverlapsBox(Box))
				{
					continue;
				}
				const bool bEdited = Volume->ChunkHasEdits(CC);
				if (!bEdited && !ChunkOverlapsSurface(CC, ChunkM))
				{
					continue;
				}
				if (AsyncInFlight.Contains(CC))
				{
					continue;
				}
				CaveChunks.Add(CC);
				HollowChunks.Remove(CC);
				BrushForceLOD0.Add(CC);
				LastRemeshAt.Remove(CC);
				MeshQueued.Remove(CC);
				NearMeshQueue.Remove(CC);
				MeshQueue.Remove(CC);
				LastRemeshAt.Add(CC, Now);
				BuildChunkMeshSync(CC);
				++N;
			}
		}
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s island remesh n=%d spheres=%d"),
		GX_VERSION_STRING, N, EditIsland.Spheres.Num());
	GX_PERF(1, TEXT("GX-island remesh n=%d spheres=%d"), N, EditIsland.Spheres.Num());
}

void AGXVoxelWorld::RemeshCaveAt(const FVector& LocalM, float RadiusM, bool bOnlyExistingCaves)
{
	if (!Volume)
	{
		return;
	}
	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const float Cover = FMath::Max(RadiusM, 1.0f) + 8.0f;
	const int32 Reach = FMath::CeilToInt(Cover / ChunkM) + 1;
	const FGXChunkKey Center = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(LocalM.X, LocalM.Y, LocalM.Z), VoxelSize));
	const FVector4 Ball(LocalM.X, LocalM.Y, LocalM.Z, RadiusM + VoxelSize * 1.35f);
	struct FCand
	{
		FGXChunkKey Key;
		float Ds = 0.0f;
	};
	TArray<FCand> Cands;
	for (int32 Z = -Reach; Z <= Reach; ++Z)
	{
		for (int32 Y = -Reach; Y <= Reach; ++Y)
		{
			for (int32 X = -Reach; X <= Reach; ++X)
			{
				const FGXChunkKey CC(Center.X + X, Center.Y + Y, Center.Z + Z);
				const FVector C((CC.X + 0.5f) * ChunkM, (CC.Y + 0.5f) * ChunkM, (CC.Z + 0.5f) * ChunkM);
				const float Ds = FVector::DistSquared(C, LocalM);
				if (Ds > FMath::Square(Cover + ChunkM))
				{
					continue;
				}
				if (!Volume->ChunkHasEdits(CC))
				{
					continue;
				}
				if (bOnlyExistingCaves && !CaveChunks.Contains(CC))
				{
					continue;
				}
				if (AsyncInFlight.Contains(CC))
				{
					continue;
				}
				FCand& Cand = Cands.AddDefaulted_GetRef();
				Cand.Key = CC;
				Cand.Ds = Ds;
			}
		}
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Ds < B.Ds; });
	const int32 Take = FMath::Min(6, Cands.Num());
	int32 N = 0;
	const double Now = FPlatformTime::Seconds();
	for (int32 I = 0; I < Take; ++I)
	{
		const FGXChunkKey CC = Cands[I].Key;
		CaveChunks.Add(CC);
		TArray<FVector4>& Balls = CarveBalls.FindOrAdd(CC);
		if (Balls.Num() >= 12)
		{
			Balls.RemoveAt(0);
		}
		Balls.Add(Ball);
		HollowChunks.Remove(CC);
		BrushForceLOD0.Add(CC);
		LastRemeshAt.Remove(CC);
		MeshQueued.Remove(CC);
		NearMeshQueue.Remove(CC);
		MeshQueue.Remove(CC);
		LastRemeshAt.Add(CC, Now);
		BuildChunkMeshSync(CC);
		++N;
	}
	GX_PERF(1, TEXT("GX-cave remesh n=%d take=%d cover=%.1f balls=%d live=%d"),
		Cands.Num(), N, Cover, CarveBalls.Num(), CaveChunks.Num());
}

bool AGXVoxelWorld::HasCaveVisualNear(const FVector& LocalM, float RadiusM) const
{
	if (CaveChunks.Num() == 0 || ChunkVisuals.Num() == 0)
	{
		return false;
	}
	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const float Reach2 = FMath::Square(FMath::Max(RadiusM, 1.0f) + ChunkM);
	for (const auto& Pair : ChunkVisuals)
	{
		if (!CaveChunks.Contains(Pair.Key))
		{
			continue;
		}
		if (Pair.Value.VertCount < 3 || Pair.Value.IndexCount < 3)
		{
			continue;
		}
		const FVector C((Pair.Key.X + 0.5f) * ChunkM, (Pair.Key.Y + 0.5f) * ChunkM, (Pair.Key.Z + 0.5f) * ChunkM);
		if (FVector::DistSquared(C, LocalM) <= Reach2)
		{
			return true;
		}
	}
	return false;
}

void AGXVoxelWorld::CollectCavePointsNear(const FVector& LocalM, float RadiusM, TArray<FVector>& Out) const
{
	if (CaveChunks.Num() == 0 || ChunkVisuals.Num() == 0 || RadiusM <= 0.0f)
	{
		return;
	}
	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const float R2 = RadiusM * RadiusM;
	const float Reach2 = FMath::Square(RadiusM + ChunkM);
	for (const auto& Pair : ChunkVisuals)
	{
		if (!CaveChunks.Contains(Pair.Key) || Pair.Value.VertCount < 3)
		{
			continue;
		}
		const FVector Center(
			(Pair.Key.X + 0.5f) * ChunkM,
			(Pair.Key.Y + 0.5f) * ChunkM,
			(Pair.Key.Z + 0.5f) * ChunkM);
		if (FVector::DistSquared(Center, LocalM) > Reach2)
		{
			continue;
		}
		UProceduralMeshComponent* PMC = MeshBanks.IsValidIndex(Pair.Value.Bank)
			? const_cast<UProceduralMeshComponent*>(MeshBanks[Pair.Value.Bank].Get())
			: nullptr;
		if (!PMC)
		{
			continue;
		}
		const FProcMeshSection* Sec = PMC->GetProcMeshSection(Pair.Value.Section);
		if (!Sec)
		{
			continue;
		}
		for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
		{
			const FVector P = V.Position * 0.01f;
			if (FVector::DistSquared(P, LocalM) <= R2)
			{
				Out.Add(P);
			}
		}
	}
}

bool AGXVoxelWorld::ShouldPunchClipmap(const FVector& LocalM) const
{
	if (!Volume)
	{
		return false;
	}
	FGXVoxelPacked Stored;
	const FVector3d P(LocalM.X, LocalM.Y, LocalM.Z);
	// Only the surface itself. Air 0.5–0.9 m under a solid cap is a cave
	// or a thin place fill — punching that opened far-hill holes (0140).
	if (Volume->TryGetAuthoritative(P, Stored) && Stored.ToDensityMeters() <= 0.0f)
	{
		return true;
	}
	return false;
}

void AGXVoxelWorld::MarkPersistDirty()
{
	bPersistDirty = true;
}
