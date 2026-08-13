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
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "GXBodyMovement.h"
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
	P.CrustDepth = FMath::Max(12.0f, MaxRelief * 0.12f);
	Volume = MakeUnique<FGXVoxelVolume>(P);
	Jobs = MakeUnique<FGXJobGraph>();
}

void AGXVoxelWorld::SetupDistantSphere()
{
	if (!DistantPlanetSphere)
	{
		return;
	}
	const float WorldRadiusCm = PlanetRadius * GMetersToUU * 0.97f;
	DistantPlanetSphere->SetRelativeScale3D(FVector(WorldRadiusCm / 50.0f));
	// Hidden at walkable scale — it reads as a second uneditable grass layer under holes.
	DistantPlanetSphere->SetVisibility(false);
	if (UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface))
	{
		DistantPlanetSphere->SetMaterial(0, DefaultMat);
	}
}

void AGXVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogGXVoxel, Warning, TEXT("GX-%s AGXVoxelWorld BeginPlay radius=%.0f stream=%.0f async=%d"),
		GX_VERSION_STRING, PlanetRadius, StreamRadius, bAsyncMeshing ? 1 : 0);
	RebuildParams();
	SetupDistantSphere();
	WarmupTimeRemaining = WarmupSeconds;
	TerrainPBR = MakeUnique<FGXTerrainPBR>();
	TerrainPBR->Initialize(this);
	if (UMaterialInterface* PBR = TerrainPBR->GetMaterial())
	{
		TerrainMaterial = PBR;
	}
	else
	{
		LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}

	if (bAutoLoadOnBeginPlay)
	{
		LoadWorld();
	}

	UpdateStreaming(GetPrimaryInvokerLocation());
	FlushMeshQueue(WarmupMeshBuildsPerFrame);
}

void AGXVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAutoSaveOnEndPlay)
	{
		SaveWorld();
	}
	if (TerrainPBR)
	{
		TerrainPBR->Shutdown();
		TerrainPBR.Reset();
	}
	if (Jobs)
	{
		Jobs->Flush(2.0f);
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
	if (DistantPlanetSphere)
	{
		DistantPlanetSphere->SetVisibility(false);
	}
	StreamCooldown -= DeltaSeconds;
	double StreamMs = 0.0;
	const float MovedSq = FVector::DistSquared(CachedViewerWorld, LastStreamViewerWorld);
	if (StreamCooldown <= 0.0f || MovedSq > FMath::Square(600.0f) || WarmupTimeRemaining > 0.0f)
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
	const int32 QueueBefore = MeshQueue.Num();
	ProcessMeshQueue(Budget);
	const double MeshMs = (FPlatformTime::Seconds() - M0) * 1000.0;
	RefreshLoadState();

	const double TickMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	static double LogAcc = 0.0;
	LogAcc += DeltaSeconds;
	if (LogAcc >= 1.0)
	{
		LogAcc = 0.0;
		UE_LOG(LogGXVoxel, Warning,
			TEXT("GX-%s perf tick=%.1fms stream=%.1fms meshApply=%.1fms dt=%.1fms fps~%.1f chunks=%d hollow=%d queue=%d->%d ready=%d status=%s playerR=%.0fm"),
			GX_VERSION_STRING,
			TickMs, StreamMs, MeshMs,
			DeltaSeconds * 1000.0,
			DeltaSeconds > KINDA_SMALL_NUMBER ? 1.0 / DeltaSeconds : 0.0,
			ChunkActors.Num(), HollowChunks.Num(),
			QueueBefore, MeshQueue.Num(),
			bWorldReady ? 1 : 0,
			*LoadStatus,
			WorldToLocalMeters(CachedViewerWorld).Size());
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
	const FVector Surface = FindSurfaceWorldLocation(FVector(1, 0, 0));
	const FVector Up = -GetGravityDirectionAt(Surface);
	return Surface + Up * 200.0f;
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
	return Volume ? Volume->SampleDensity(PlanetLocalMeters) : -1.0f;
}

int32 AGXVoxelWorld::SampleMaterial(const FVector3d& PlanetLocalMeters) const
{
	return Volume ? Volume->Sample(PlanetLocalMeters).Material : 0;
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
	const FVector Center = GetActorLocation();
	const float OuterCm = (PlanetRadius + MaxRelief + 100.0f) * GMetersToUU;
	const FGXVoxelHit Hit = RaycastVoxels(Center + Dir * OuterCm, -Dir, OuterCm + 2000.0f);
	if (Hit.bHit)
	{
		return Hit.Location;
	}
	return Center + Dir * (PlanetRadius * GMetersToUU);
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
		EnqueueRemeshNeighborhood(C);
	}
	FlushMeshQueue(48);
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
		EnqueueRemeshNeighborhood(C);
	}
	FlushMeshQueue(48);
	return Out;
}

int32 AGXVoxelWorld::SelectLOD(float DistanceM) const
{
	if (bForceLOD0 || WarmupTimeRemaining > 0.0f || DistanceM < NearFieldRadius)
	{
		return 0;
	}
	if (DistanceM < StreamRadius * 0.65f) return 1;
	return 2;
}

void AGXVoxelWorld::RefreshLoadState()
{
	LastMeshedNear = 0;
	int32 HollowNear = 0;
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
			++HollowNear;
		}
	}

	const int32 Queue = MeshQueue.Num() + AsyncInFlight.Num();
	const int32 Desired = FMath::Max(LastDesiredNear, LastMeshedNear + HollowNear);
	const int32 Resolved = FMath::Min(LastMeshedNear + HollowNear, Desired);
	const float MeshFrac = (Desired > 0)
		? static_cast<float>(Resolved) / static_cast<float>(Desired)
		: 0.0f;
	const float QueueFrac = (Queue <= 0) ? 1.0f : FMath::Clamp(1.0f - Queue / 80.0f, 0.0f, 0.85f);

	if (LastMeshedNear == 0 && Resolved == 0)
	{
		LoadStatus = TEXT("Generating crust density…");
		LoadProgress = 0.08f;
	}
	else if (Queue > 0 && MeshFrac < 0.95f)
	{
		LoadStatus = FString::Printf(TEXT("Meshing near-field terrain  %d / %d"), Resolved, FMath::Max(Desired, 1));
		LoadProgress = 0.10f + 0.70f * MeshFrac;
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
	const bool bHaveGround = LastMeshedNear >= 4;
	const bool bNearFilled = Desired == 0 || MeshFrac >= 0.85f;
	const bool bQueueQuiet = Queue <= 2;
	if (bHaveGround && bQueueQuiet && (bNearFilled || Queue == 0) && WarmupTimeRemaining <= 0.0f)
	{
		LoadStatus = TEXT("Ready");
		LoadProgress = 1.0f;
		bWorldReady = true;
	}
}

void AGXVoxelWorld::InvalidateHollow(const FGXChunkKey& Coord)
{
	HollowChunks.Remove(Coord);
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

void AGXVoxelWorld::EnqueueRemesh(const FGXChunkKey& Coord)
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
	MeshQueue.Add(Coord);
}

void AGXVoxelWorld::UpdateStreaming(FVector WorldViewerLocation)
{
	if (!Volume)
	{
		return;
	}
	const FVector Local = WorldToLocalMeters(WorldViewerLocation);
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const int32 ChunkRadius = FMath::CeilToInt(StreamRadius / ChunkM) + 1;
	const FGXChunkKey Center = FGXVoxelVolume::VoxelToChunk(
		FGXVoxelVolume::WorldToVoxel(FVector3d(Local.X, Local.Y, Local.Z), VoxelSize));

	const float ShellInner = PlanetRadius - MaxRelief - 24.0f;
	const float ShellOuter = PlanetRadius + MaxRelief + 16.0f;
	const float ChunkHalf = ChunkM * 0.866f;

	int32 NearWanted = 0;
	TSet<FGXChunkKey> Desired;
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
				if (Dist > StreamRadius)
				{
					continue;
				}
				const float R = ChunkCenter.Size();
				if ((R + ChunkHalf) < ShellInner || (R - ChunkHalf) > ShellOuter)
				{
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
				const TWeakObjectPtr<AGXVoxelChunkProxy>* Existing = ChunkActors.Find(CC);
				const bool bHaveMesh = Existing && Existing->IsValid() && Existing->Get()->HasRenderableMesh();
				const bool bNeedCollision = Dist <= CollisionRadius && bHaveMesh && !Existing->Get()->HasCollision();
				if (!bHaveMesh || bNeedCollision)
				{
					EnqueueRemesh(CC);
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
}

void AGXVoxelWorld::FlushMeshQueue(int32 MaxBuilds)
{
	ProcessMeshQueue(FMath::Max(0, MaxBuilds));
	DrainPendingMeshes(MaxBuilds);
}

void AGXVoxelWorld::ProcessMeshQueue(int32 Budget)
{
	int32 Built = 0;
	const bool bAsync = bAsyncMeshing && WarmupTimeRemaining <= 0.0f && Jobs.IsValid();
	while (Built < Budget && MeshQueue.Num() > 0)
	{
		const FGXChunkKey Coord = MeshQueue.Pop(EAllowShrinking::No);
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
		const bool bSync = !bAsync || BrushForceLOD0.Contains(Coord);
		if (bSync)
		{
			BuildChunkMeshSync(Coord);
		}
		else
		{
			EnqueueChunkMeshAsync(Coord);
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
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume->PublishSnapshot();
	const FVector Local = WorldToLocalMeters(CachedViewerWorld.IsNearlyZero() ? GetPrimaryInvokerLocation() : CachedViewerWorld);
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const int32 LOD = BrushForceLOD0.Contains(Coord) ? 0 : SelectLOD(FVector::Dist(Center, Local));
	BrushForceLOD0.Remove(Coord);
	FGXMesher::FSettings S;
	S.LOD = LOD;
	FGXMeshBuffers Mesh = FGXMesher::MeshChunk(*Snap, Coord, S);
	ApplyBuiltMesh(Coord, LOD, MoveTemp(Mesh));
}

void AGXVoxelWorld::EnqueueChunkMeshAsync(const FGXChunkKey& Coord)
{
	if (!Volume || !Jobs || AsyncInFlight.Contains(Coord))
	{
		return;
	}
	AsyncInFlight.Add(Coord);
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume->PublishSnapshot();
	if (Jobs)
	{
		Snap->Stamp = Jobs->GetStamp();
	}
	const FGXGenerationStamp Stamp = Snap->Stamp;
	const FVector Local = WorldToLocalMeters(GetPrimaryInvokerLocation());
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const int32 LOD = BrushForceLOD0.Contains(Coord) ? 0 : SelectLOD(FVector::Dist(Center, Local));
	BrushForceLOD0.Remove(Coord);

	Jobs->Enqueue(EGXJobPriority::NearMesh, Stamp,
		[this, Coord, LOD, Snap, Stamp]()
		{
			FGXMesher::FSettings S;
			S.LOD = LOD;
			FGXMeshBuffers Built = FGXMesher::MeshChunk(*Snap, Coord, S);
			FScopeLock Lock(&PendingCS);
			FPendingMesh& P = PendingMeshes.AddDefaulted_GetRef();
			P.Coord = Coord;
			P.LOD = LOD;
			P.Stamp = Stamp;
			P.Mesh = MoveTemp(Built);
		});
}

void AGXVoxelWorld::DrainPendingMeshes(int32 Budget)
{
	TArray<FPendingMesh> Local;
	{
		FScopeLock Lock(&PendingCS);
		const int32 N = FMath::Min(Budget, PendingMeshes.Num());
		for (int32 I = 0; I < N; ++I)
		{
			Local.Add(MoveTemp(PendingMeshes[I]));
		}
		PendingMeshes.RemoveAt(0, N, EAllowShrinking::No);
	}
	for (FPendingMesh& P : Local)
	{
		AsyncInFlight.Remove(P.Coord);
		if (RemeshWhenIdle.Remove(P.Coord))
		{
			EnqueueRemesh(P.Coord);
		}
		if (Jobs && !Jobs->ShouldApply(P.Stamp))
		{
			continue;
		}
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
		HollowChunks.Add(Coord);
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
	const bool bCollision = FVector::Dist(CenterM, ViewerLocal) <= CollisionRadius || WarmupTimeRemaining > 0.0f;
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
	UpdateStreaming(SpawnLoc);
	FlushMeshQueue(256);

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
	UpdateStreaming(CachedViewerWorld);
	FlushMeshQueue(96);

	UE_LOG(LogGXVoxel, Warning,
		TEXT("GX-%s PlacePawnOnSurface r=%.1fm want=%.1fm loc=%s"),
		GX_VERSION_STRING,
		WorldToLocalMeters(Pawn->GetActorLocation()).Size(),
		PlanetRadius,
		*Pawn->GetActorLocation().ToCompactString());
	return true;
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
