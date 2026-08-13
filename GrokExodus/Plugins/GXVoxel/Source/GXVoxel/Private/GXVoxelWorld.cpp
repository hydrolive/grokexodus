// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelWorld.h"
#include "GXMesher.h"
#include "GXVoxelChunkProxy.h"
#include "GXVoxelInvokerComponent.h"
#include "GXVoxelVolume.h"
#include "GXGravity.h"
#include "GXMath.h"
#include "GXVoxel.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

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
	DistantPlanetSphere->SetVisibility(true);
	if (UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface))
	{
		DistantPlanetSphere->SetMaterial(0, DefaultMat);
	}
}

void AGXVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	RebuildParams();
	SetupDistantSphere();
	WarmupTimeRemaining = WarmupSeconds;

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
	if (WarmupTimeRemaining > 0.0f)
	{
		WarmupTimeRemaining -= DeltaSeconds;
	}

	UpdateStreaming(GetPrimaryInvokerLocation());
	DrainPendingMeshes(MaxMeshBuildsPerFrame * 2);

	const int32 Budget = (WarmupTimeRemaining > 0.0f) ? WarmupMeshBuildsPerFrame : MaxMeshBuildsPerFrame;
	ProcessMeshQueue(Budget);
}

FVector AGXVoxelWorld::GetPrimaryInvokerLocation() const
{
	FVector Best = FindSurfaceWorldLocation(FVector(1, 0, 0)) + FVector(200, 0, 0);
	float BestScore = -1.0f;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UGXVoxelInvokerComponent* Inv = It->FindComponentByClass<UGXVoxelInvokerComponent>())
			{
				if (!Inv->bEnabled)
				{
					continue;
				}
				const FVector Loc = Inv->GetInvokerWorldLocation();
				const float Dist = WorldToLocalMeters(Loc).Size();
				if (Dist > PlanetRadius * 0.4f)
				{
					return Loc;
				}
				if (Dist > BestScore)
				{
					BestScore = Dist;
					Best = Loc;
				}
			}
		}
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			const float Dist = WorldToLocalMeters(Pawn->GetActorLocation()).Size();
			if (Dist > PlanetRadius * 0.4f)
			{
				return Pawn->GetActorLocation();
			}
		}
	}
	return Best;
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
		EnqueueRemesh(C);
	}
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
		EnqueueRemesh(C);
	}
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

void AGXVoxelWorld::EnqueueRemesh(const FGXChunkKey& Coord)
{
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
				const TWeakObjectPtr<AGXVoxelChunkProxy>* Existing = ChunkActors.Find(CC);
				const bool bNeedMesh = !Existing || !Existing->IsValid() || !Existing->Get()->HasRenderableMesh();
				if (bNeedMesh)
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
		const FGXChunkKey Coord = MeshQueue[0];
		MeshQueue.RemoveAt(0);
		MeshQueued.Remove(Coord);
		if (bAsync)
		{
			EnqueueChunkMeshAsync(Coord);
		}
		else
		{
			BuildChunkMeshSync(Coord);
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
	const FVector Local = WorldToLocalMeters(GetPrimaryInvokerLocation());
	const float ChunkM = VoxelSize * FGXVoxelConstants::ChunkSize;
	const FVector Center((Coord.X + 0.5f) * ChunkM, (Coord.Y + 0.5f) * ChunkM, (Coord.Z + 0.5f) * ChunkM);
	const int32 LOD = SelectLOD(FVector::Dist(Center, Local));
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
	const int32 LOD = SelectLOD(FVector::Dist(Center, Local));

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
		if (AGXVoxelChunkProxy* Empty = ChunkActors.FindRef(Coord).Get())
		{
			if (!Empty->HasRenderableMesh())
			{
				Empty->Destroy();
				ChunkActors.Remove(Coord);
			}
		}
		return;
	}

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
	Proxy->ApplyMesh(MeshData, OriginM, GMetersToUU, TerrainMaterial, LOD == 0);
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
