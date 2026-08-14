// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelWorld.h"
#include "GXMesher.h"
#include "GXVoxelChunkProxy.h"
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

static constexpr float GMetersToUU = 100.0f;
static constexpr float GUUToMeters = 0.01f;

namespace GXPersist
{
	static constexpr uint32 Magic = 0x31565847; // GXV1
	static constexpr uint32 Version = 1;
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
	MeshQueue.Empty();
	MeshQueued.Empty();
	AsyncInFlight.Empty();
	HollowChunks.Empty();
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
	StreamRadius = FMath::Clamp(InStream, 200.0f, 900.0f);
	UnloadRadius = StreamRadius + 120.0f;
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
	StreamRadius = 360.0f;
	UnloadRadius = 500.0f;
	NearFieldRadius = 110.0f;
	// Walk-floor collision. 320 m cooked too many meshes (77 ms apply).
	CollisionRadius = 240.0f;
	StreamInterval = 0.55f;
	bForceLOD0 = false;
	HorizonOuterM = 10000.0f;
	bAsyncMeshing = true;
	WarmupSeconds = 1.0f;
	WarmupMeshBuildsPerFrame = 4;
	MaxMeshBuildsPerFrame = 6;
	MeshTimeBudgetMs = 6.0f;
	MaxAsyncInFlight = 12;
	bAutoLoadOnBeginPlay = false;
	ConfigurePlanet(E.Radius, E.MaxRelief, StreamRadius, static_cast<int32>(E.Seed));
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
	if (HorizonClipmap)
	{
		HorizonClipmap->Shutdown();
		HorizonClipmap.Reset();
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
	DrainPendingMeshes(MaxMeshBuildsPerFrame);
	const int32 Budget = (WarmupTimeRemaining > 0.0f) ? WarmupMeshBuildsPerFrame : MaxMeshBuildsPerFrame;
	const int32 QueueBefore = NearMeshQueue.Num() + MeshQueue.Num();
	ProcessMeshQueue(Budget);
	const double MeshMs = (FPlatformTime::Seconds() - M0) * 1000.0;
	if (HorizonClipmap && Volume && bAtlasReady)
	{
		HorizonClipmap->Update(
			this,
			Volume->GetStamp(),
			WorldToLocalMeters(CachedViewerWorld),
			StreamRadius,
			HorizonOuterM,
			nullptr);
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
			ChunkActors.Num(), HollowChunks.Num(),
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
	const FVector Dir = WorldDirection.GetSafeNormal();
	const float StepCm = FMath::Max(VoxelSize * GMetersToUU * 0.35f, 25.0f);
	float PrevD = SampleDensityWorld(WorldOrigin);
	for (float T = StepCm; T <= MaxDistance; T += StepCm)
	{
		const FVector Pos = WorldOrigin + Dir * T;
		const float D = SampleDensityWorld(Pos);
		if (PrevD < 0.0f && D >= 0.0f)
		{
			float T0 = T - StepCm, T1 = T;
			for (int32 I = 0; I < 8; ++I)
			{
				const float Tm = 0.5f * (T0 + T1);
				if (SampleDensityWorld(WorldOrigin + Dir * Tm) >= 0.0f) T1 = Tm;
				else T0 = Tm;
			}
			const FVector HitPos = WorldOrigin + Dir * T1;
			const FVector Local = WorldToLocalMeters(HitPos);
			Hit.bHit = true;
			Hit.Location = HitPos;
			Hit.Distance = T1;
			Hit.MaterialId = SampleMaterial(FVector3d(Local.X, Local.Y, Local.Z));
			const float E = VoxelSize * 0.5f;
			const FVector3d LM(Local.X, Local.Y, Local.Z);
			const float Dx = SampleDensityMeters(LM + FVector3d(E, 0, 0)) - SampleDensityMeters(LM - FVector3d(E, 0, 0));
			const float Dy = SampleDensityMeters(LM + FVector3d(0, E, 0)) - SampleDensityMeters(LM - FVector3d(0, E, 0));
			const float Dz = SampleDensityMeters(LM + FVector3d(0, 0, E)) - SampleDensityMeters(LM - FVector3d(0, 0, E));
			FVector N(-Dx, -Dy, -Dz);
			if (!N.Normalize()) N = -Dir;
			Hit.Normal = N;
			return Hit;
		}
		PrevD = D;
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

FGXDigOutcome AGXVoxelWorld::DigSphere(FVector WorldCenter, float RadiusM, float DigSpeedMul, float RecoveryMul, float WearMul)
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
		BrushForceLOD0.Add(C);
		EnqueueRemesh(C, true);
	}
	FlushMeshQueue(2);
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
	for (const FGXChunkKey& C : Brush.DirtyChunks)
	{
		FGXCrustCache::InvalidateChunk(Volume->GetStamp().GetParams(), C);
		BrushForceLOD0.Add(C);
		EnqueueRemesh(C, true);
	}
	FlushMeshQueue(2);
	return Out;
}

int32 AGXVoxelWorld::SelectLOD(float DistanceM) const
{
	if (bForceLOD0 || DistanceM < FMath::Max(NearFieldRadius, StreamRadius * 0.55f))
	{
		return 0;
	}
	// Screenspace: keep a voxel near ~3 px at 1080p / 90° (ε ≈ v/d).
	const float WantM = FMath::Max(VoxelSize, DistanceM * 0.018f);
	if (WantM < VoxelSize * 2.0f)
	{
		return 0;
	}
	if (WantM < VoxelSize * 4.0f)
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

	for (const auto& Pair : ChunkActors)
	{
		if (IsNear(Pair.Key) && Pair.Value.IsValid() && Pair.Value->HasRenderableMesh())
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

	// A spherical near-field is mostly hollow. Ready when the queue is quiet and
	// we have real ground — do not require 85% of the ball to have a visible mesh.
	const bool bHaveGround = LastMeshedNear >= 2;
	const bool bNearFilled = Desired == 0 || MeshFrac >= 0.70f;
	const bool bNearQuiet = NearMeshQueue.Num() == 0 && AsyncInFlight.Num() <= 2;
	if (bAtlasReady && bHaveGround && (bNearQuiet || bNearFilled) && WarmupTimeRemaining <= 0.0f)
	{
		LoadStatus = TEXT("Ready");
		LoadProgress = 1.0f;
		bWorldReady = true;
	}
	GX_PERF(2, TEXT("GX-load mesh=%d hollowNear=%d desired=%d queue=%d status=%s"),
		LastMeshedNear, LastHollowNear, Desired, Queue, *LoadStatus);
}

void AGXVoxelWorld::InvalidateHollow(const FGXChunkKey& Coord)
{
	HollowChunks.Remove(Coord);
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
	if (HollowChunks.Contains(Coord))
	{
		return;
	}
	HollowChunks.Add(Coord);
	++LastSettledEmpty;
	GX_PERF(2, TEXT("GX-empty settle %d_%d_%d %s"), Coord.X, Coord.Y, Coord.Z, Reason ? Reason : TEXT("?"));
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
	if (AsyncInFlight.Contains(Coord))
	{
		RemeshWhenIdle.Add(Coord);
		return;
	}
	if (MeshQueued.Contains(Coord))
	{
		return;
	}
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
	const bool bNearBusy = NearMeshQueue.Num() > 0 || LastMeshedNear < 2;
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
				if (!ChunkOverlapsSurface(CC, ChunkM))
				{
					++SkippedAir;
					continue;
				}
				Desired.Add(CC);
				if (Dist <= NearFieldRadius)
				{
					++NearWanted;
				}
				if (HollowChunks.Contains(CC))
				{
					continue;
				}
				if (AsyncInFlight.Contains(CC))
				{
					// Already meshing. Do not stamp RemeshWhenIdle every 200 ms —
					// that re-queued the same 6 jobs forever.
					continue;
				}
				// 0.7.17 deferred everything past 110 m while near was busy.
				// Walking kept near busy, so the 110–360 m band never meshed
				// and the player walked off voxels onto the clipmap.
				if (bNearBusy && Dist > FMath::Max(NearFieldRadius, CollisionRadius + 16.0f))
				{
					++DeferredFar;
					continue;
				}
				const TWeakObjectPtr<AGXVoxelChunkProxy>* Existing = ChunkActors.Find(CC);
				const bool bHaveMesh = Existing && Existing->IsValid() && Existing->Get()->HasRenderableMesh();
				const bool bNeedCollision = Dist <= CollisionRadius && bHaveMesh && !Existing->Get()->HasCollision();
				if (!bHaveMesh || bNeedCollision)
				{
					EnqueueRemesh(CC, Dist <= NearFieldRadius);
				}
			}
		}
	}

	TArray<FGXChunkKey> ToRemove;
	for (const auto& Pair : ChunkActors)
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

	while (Built < Budget
		&& (NearMeshQueue.Num() + MeshQueue.Num()) > 0
		&& FPlatformTime::Seconds() < Deadline)
	{
		const FGXChunkKey Coord = PopNext();
		MeshQueued.Remove(Coord);
		if (AsyncInFlight.Contains(Coord))
		{
			RemeshWhenIdle.Add(Coord);
			continue;
		}
		if (HollowChunks.Contains(Coord))
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
		++CacheMisses;

		const int32 InFlight = Jobs ? Jobs->NumInFlight() : AsyncInFlight.Num();
		const bool bUnderfoot = BrushForceLOD0.Contains(Coord) && Built < 1;
		if (!bAsync || bUnderfoot)
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
	for (FGXMeshMailbox::FItem& P : Local)
	{
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
		ApplyBuiltMesh(P.Coord, P.LOD, MoveTemp(P.Mesh));
	}
}

void AGXVoxelWorld::ApplyBuiltMesh(const FGXChunkKey& Coord, int32 LOD, FGXMeshBuffers&& MeshData)
{
	if (MeshData.IsEmpty())
	{
		if (AGXVoxelChunkProxy* Existing = ChunkActors.FindRef(Coord).Get())
		{
			if (Existing->HasRenderableMesh())
			{
				return;
			}
			Existing->Destroy();
			ChunkActors.Remove(Coord);
		}
		// Session-settle. Near-surface empties used to skip this and then
		// UpdateStreaming re-enqueued them forever (32/164 stuck overlay).
		MarkChunkEmpty(Coord, LOD, TEXT("mesh"));
		return;
	}
	HollowChunks.Remove(Coord);

	const float ChunkM = VoxelSize * static_cast<float>(FGXVoxelConstants::ChunkSize);
	const FVector OriginM(Coord.X * ChunkM, Coord.Y * ChunkM, Coord.Z * ChunkM);

	TWeakObjectPtr<AGXVoxelChunkProxy>& Slot = ChunkActors.FindOrAdd(Coord);
	AGXVoxelChunkProxy* Proxy = Slot.Get();
	if (!Proxy)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SP.Owner = this;
		Proxy = GetWorld()->SpawnActor<AGXVoxelChunkProxy>(
			LocalMetersToWorld(OriginM), GetActorRotation(), SP);
		if (!Proxy)
		{
			return;
		}
		Proxy->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		Proxy->InitializeChunk(Coord, LOD);
		Slot = Proxy;
	}
	else
	{
		Proxy->SetActorLocation(LocalMetersToWorld(OriginM));
	}
	Proxy->LOD = LOD;
	const FVector ViewerLocal = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const FVector CenterM = OriginM + FVector(ChunkM * 0.5f);
	const bool bCollision = FVector::Dist(CenterM, ViewerLocal) <= CollisionRadius;
	Proxy->ApplyMesh(MeshData, OriginM, GMetersToUU, TerrainMaterial, bCollision);
}

bool AGXVoxelWorld::PlacePawnOnSurface(APawn* Pawn, FVector RadialHint)
{
	if (!Pawn || !Volume)
	{
		return false;
	}

	const FVector Surface = FindSurfaceWorldLocation(RadialHint);
	const FVector Up = -GetGravityDirectionAt(Surface);
	const FVector SpawnLoc = Surface + Up * 180.0f;
	CachedViewerWorld = SpawnLoc;
	LastStreamViewerWorld = FVector(1e12f, 0, 0);
	ActiveStreamRadius = FMath::Max(ActiveStreamRadius, 140.0f);
	EnsureCrustAtlas();
	if (bAtlasReady)
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
			Move->SnapToSurface(true);
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
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s crust atlas ready disk=%d dim=%d"),
		GX_VERSION_STRING, bFromDisk ? 1 : 0, Built->Dim);
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
	return FFileHelper::SaveArrayToFile(Buf, *GetSavePath());
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
	}
	UE_LOG(LogGXVoxel, Log, TEXT("Loaded %d dirty pages from %s"), PageCount, *GetSavePath());
	return true;
}
