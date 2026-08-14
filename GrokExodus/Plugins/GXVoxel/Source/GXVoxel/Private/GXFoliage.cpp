// Copyright Grok Exodus. All Rights Reserved.

#include "GXFoliage.h"
#include "GXNoise.h"
#include "GXVoxel.h"
#include "GXVoxelStamps.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

UStaticMesh* FGXFoliageScatter::TryLoad(const TCHAR* Path)
{
	return LoadObject<UStaticMesh>(nullptr, Path);
}

void FGXFoliageScatter::Initialize(AActor* Owner)
{
	Shutdown();
	if (!Owner)
	{
		return;
	}

	struct FSpec
	{
		const TCHAR* Path;
		float Spacing;
		float Radius;
		float SMin;
		float SMax;
		int32 Kind;
	};
	const FSpec Specs[] = {
		{ TEXT("/Game/Foliage/SM_Grass.SM_Grass"), 2.1f, 52.0f, 0.70f, 1.25f, 0 },
		{ TEXT("/Game/Foliage/SM_Bush.SM_Bush"), 6.5f, 60.0f, 0.80f, 1.40f, 1 },
		{ TEXT("/Game/Foliage/SM_Tree.SM_Tree"), 11.0f, 72.0f, 0.75f, 1.55f, 2 },
	};

	for (const FSpec& S : Specs)
	{
		UStaticMesh* Mesh = TryLoad(S.Path);
		if (!Mesh)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			Owner, NAME_None, RF_Transient);
		if (!HISM)
		{
			continue;
		}
		HISM->SetStaticMesh(Mesh);
		HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HISM->SetCastShadow(S.Kind == 2);
		HISM->SetMobility(EComponentMobility::Movable);
		HISM->bReceivesDecals = false;
		HISM->SetupAttachment(Owner->GetRootComponent());
		HISM->RegisterComponent();
		FLayer Layer;
		Layer.Comp = HISM;
		Layer.SpacingM = S.Spacing;
		Layer.RadiusM = S.Radius;
		Layer.ScaleMin = S.SMin;
		Layer.ScaleMax = S.SMax;
		Layer.Kind = S.Kind;
		Layers.Add(Layer);
		bHasMeshes = true;
	}

	if (!bHasMeshes)
	{
		UE_LOG(LogGXVoxel, Warning,
			TEXT("GXFoliage: no meshes at /Game/Foliage/SM_{Grass,Bush,Tree}. Import Brushify (or any) static meshes there — do not convert the voxel planet to a Landscape."));
	}
	else
	{
		UE_LOG(LogGXVoxel, Warning, TEXT("GXFoliage: %d HISM layers ready"), Layers.Num());
	}
}

void FGXFoliageScatter::Shutdown()
{
	Clear();
	for (FLayer& L : Layers)
	{
		if (UHierarchicalInstancedStaticMeshComponent* C = L.Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	Layers.Reset();
	bHasMeshes = false;
}

void FGXFoliageScatter::Clear()
{
	for (FLayer& L : Layers)
	{
		if (UHierarchicalInstancedStaticMeshComponent* C = L.Comp.Get())
		{
			C->ClearInstances();
		}
	}
}

bool FGXFoliageScatter::AllowAt(
	const FGXSphereStamp& Stamp,
	const FVector3f& Dir,
	int32 Kind,
	int32 MaterialId,
	float SlopeDeg,
	float HeightM)
{
	if (MaterialId == static_cast<int32>(EGXVoxelMaterial::SnowIce)
		|| MaterialId == static_cast<int32>(EGXVoxelMaterial::RockyCliff)
		|| MaterialId == static_cast<int32>(EGXVoxelMaterial::VolcanicScorched)
		|| MaterialId == static_cast<int32>(EGXVoxelMaterial::BedrockDeep)
		|| MaterialId == static_cast<int32>(EGXVoxelMaterial::Air))
	{
		return false;
	}

	const float Relief = FMath::Max(Stamp.GetParams().MaxRelief, 1.0f);
	if (HeightM < Relief * 0.02f || HeightM > Relief * 0.62f)
	{
		return false;
	}

	if (Kind == 0)
	{
		return SlopeDeg < 36.0f
			&& (MaterialId == static_cast<int32>(EGXVoxelMaterial::TemperateGrass)
				|| MaterialId == static_cast<int32>(EGXVoxelMaterial::DryDirt)
				|| MaterialId == static_cast<int32>(EGXVoxelMaterial::WetMud));
	}
	if (Kind == 1)
	{
		return SlopeDeg < 28.0f
			&& MaterialId == static_cast<int32>(EGXVoxelMaterial::TemperateGrass);
	}
	return SlopeDeg < 22.0f
		&& MaterialId == static_cast<int32>(EGXVoxelMaterial::TemperateGrass)
		&& Stamp.SampleMoisture(Dir) > 0.48f;
}

void FGXFoliageScatter::Sync(
	AActor* Owner,
	const FGXSphereStamp& Stamp,
	const FVector& ViewerWorldCm,
	float PlanetRadiusM)
{
	if (!bHasMeshes || !Owner)
	{
		return;
	}
	if (FVector::DistSquared(ViewerWorldCm, LastViewerCm) < FMath::Square(900.0f))
	{
		return;
	}
	LastViewerCm = ViewerWorldCm;

	const FVector Center = Owner->GetActorLocation();
	const FVector LocalM = (ViewerWorldCm - Center) * 0.01f;
	FVector Up = LocalM.GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = FVector(1, 0, 0);
	}
	FVector Tangent, Bitangent;
	Up.FindBestAxisVectors(Tangent, Bitangent);

	const uint32 Seed = Stamp.GetParams().Seed;
	constexpr float MetersToCm = 100.0f;

	for (FLayer& Layer : Layers)
	{
		UHierarchicalInstancedStaticMeshComponent* C = Layer.Comp.Get();
		if (!C)
		{
			continue;
		}
		C->ClearInstances();

		const int32 N = FMath::CeilToInt(Layer.RadiusM / Layer.SpacingM);
		int32 Added = 0;
		for (int32 J = -N; J <= N; ++J)
		{
			for (int32 I = -N; I <= N; ++I)
			{
				const float X = static_cast<float>(I) * Layer.SpacingM;
				const float Y = static_cast<float>(J) * Layer.SpacingM;
				if ((X * X + Y * Y) > Layer.RadiusM * Layer.RadiusM)
				{
					continue;
				}
				const uint32 H = FGXNoise::Hash(I + 4096, J + 4096, Layer.Kind, Seed + 501u);
				const float Jx = (FGXNoise::HashToFloat(H) - 0.5f) * Layer.SpacingM * 0.85f;
				const float Jy = (FGXNoise::HashToFloat(H ^ 0xA511E9B3u) - 0.5f) * Layer.SpacingM * 0.85f;
				const FVector Offset = Tangent * (X + Jx) + Bitangent * (Y + Jy);
				FVector DirV = (Up * PlanetRadiusM + Offset).GetSafeNormal();
				if (DirV.IsNearlyZero())
				{
					continue;
				}
				const FVector3f Dir(DirV.X, DirV.Y, DirV.Z);
				const float HeightM = Stamp.SampleHeightDisplacement(Dir) - Stamp.SampleScarCarveMeters(Dir);
				const FVector SurfaceM = DirV * (static_cast<double>(PlanetRadiusM) + HeightM);
				const FVector3d Surf3(SurfaceM.X, SurfaceM.Y, SurfaceM.Z);
				const float Dens = Stamp.SampleDensity(Surf3);
				if (Dens < -1.5f || Dens > 2.5f)
				{
					continue;
				}
				const int32 Mat = Stamp.SampleMaterial(Surf3, FMath::Max(Dens, 0.05f));
				const float Slope = Stamp.SampleSlopeDegrees(Dir, 3.5f);
				if (!AllowAt(Stamp, Dir, Layer.Kind, Mat, Slope, HeightM))
				{
					continue;
				}

				const float Yaw = FGXNoise::HashToFloat(H ^ 0x27D4EB2Fu) * 360.0f;
				const float Scl = FMath::Lerp(Layer.ScaleMin, Layer.ScaleMax, FGXNoise::HashToFloat(H ^ 0x165667B1u));
				const FVector World = Center + SurfaceM * MetersToCm;
				const FQuat Q = FRotationMatrix::MakeFromZX(DirV, Tangent.RotateAngleAxis(Yaw, DirV)).ToQuat();
				C->AddInstance(FTransform(Q, World, FVector(Scl)), true);
				++Added;
			}
		}
		C->MarkRenderStateDirty();
		UE_LOG(LogGXVoxel, Verbose, TEXT("GXFoliage layer %d instances=%d"), Layer.Kind, Added);
	}
}
