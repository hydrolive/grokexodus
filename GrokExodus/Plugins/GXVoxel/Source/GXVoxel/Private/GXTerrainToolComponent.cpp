// Copyright Grok Exodus. All Rights Reserved.

#include "GXTerrainToolComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"

UGXTerrainToolComponent::UGXTerrainToolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGXTerrainToolComponent::BeginPlay()
{
	Super::BeginPlay();
	TryFindWorld();

	if (AActor* Owner = GetOwner())
	{
		PreviewMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("GXBrushPreview"));
		PreviewMesh->SetupAttachment(Owner->GetRootComponent());
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetCanEverAffectNavigation(false);
		PreviewMesh->SetCastShadow(false);
		PreviewMesh->SetAbsolute(true, true, true);
		if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		{
			PreviewMesh->SetStaticMesh(Sphere);
		}
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this))
			{
				MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.45f, 0.1f));
				PreviewMesh->SetMaterial(0, MID);
			}
		}
		PreviewMesh->RegisterComponent();
	}
}

void UGXTerrainToolComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PreviewMesh)
	{
		PreviewMesh->DestroyComponent();
		PreviewMesh = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UGXTerrainToolComponent::TryFindWorld()
{
	if (World || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
	{
		World = *It;
		break;
	}
}

void UGXTerrainToolComponent::CycleMode()
{
	Mode = (Mode == EGXToolMode::Drill) ? EGXToolMode::Place : EGXToolMode::Drill;
}

void UGXTerrainToolComponent::PrimaryFire(bool bPressed)
{
	bPrimaryHeld = bPressed;
}

void UGXTerrainToolComponent::CyclePlaceMaterial(int32 Direction)
{
	PlaceMaterialId += Direction;
	if (PlaceMaterialId < 1) PlaceMaterialId = 12;
	if (PlaceMaterialId > 12) PlaceMaterialId = 1;
}

float UGXTerrainToolComponent::GetTotalStock() const
{
	float S = 0.0f;
	for (const auto& Pair : MaterialStock)
	{
		S += Pair.Value;
	}
	return S;
}

FVector UGXTerrainToolComponent::GetTraceStart() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>())
		{
			return Cam->GetComponentLocation();
		}
		return Pawn->GetActorLocation();
	}
	return FVector::ZeroVector;
}

FVector UGXTerrainToolComponent::GetTraceDir() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const UCameraComponent* Cam = Pawn->FindComponentByClass<UCameraComponent>())
		{
			return Cam->GetForwardVector();
		}
		return Pawn->GetActorForwardVector();
	}
	return FVector::ForwardVector;
}

FVector UGXTerrainToolComponent::BrushCenterFromHit(const FGXVoxelHit& Hit, const FVector& AimDir) const
{
	// Sit the ball into the surface at the hit. Pushing along the look
	// ray slid the hole sideways on angled ground (0.8.14 #2).
	const FVector Aim = AimDir.GetSafeNormal();
	FVector Out = Hit.Normal.GetSafeNormal();
	if (Out.IsNearlyZero())
	{
		Out = -Aim;
	}
	const float IntoAlongAim = FVector::DotProduct(Aim, -Out);
	FVector Into = (IntoAlongAim > 0.45f) ? -Out : Aim;
	if (Into.IsNearlyZero())
	{
		Into = Aim;
	}
	// Sit the ball into the hit so one click matches the preview
	// hemisphere. The walk disk now opens with the volume (0.8.20).
	return Hit.Location + Into * (BrushRadiusM * 0.35f * 100.0f);
}

void UGXTerrainToolComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!World)
	{
		TryFindWorld();
	}
	FireCooldown = FMath::Max(0.0f, FireCooldown - DeltaTime);
	if (bPrimaryHeld && FireCooldown <= 0.0f && World)
	{
		ApplyTool();
		FireCooldown = 0.14f;
	}

	const FVector Start = GetTraceStart();
	const FVector Dir = GetTraceDir();
	bool bHit = false;
	FVector PreviewAt = FVector::ZeroVector;
	if (World)
	{
		const FGXVoxelHit Hit = World->RaycastVoxels(Start, Dir, Reach);
		if (Hit.bHit)
		{
			PreviewAt = Hit.Location;
			if (Mode == EGXToolMode::Drill)
			{
				PreviewAt = BrushCenterFromHit(Hit, Dir);
			}
			bHit = true;
		}
	}

	if (PreviewMesh)
	{
		PreviewMesh->SetVisibility(bHit);
		PreviewMesh->SetHiddenInGame(!bHit);
		if (!bHit)
		{
			PreviewMesh->SetWorldScale3D(FVector(0.001f));
			PreviewMesh->SetWorldLocation(Start - FVector(0, 0, 100000.f));
		}
	}
	if (!bHit)
	{
		return;
	}

	const FColor Col = Mode == EGXToolMode::Drill ? FColor(255, 140, 20) : FColor(40, 200, 220);
	if (bDrawDebugPreview && GetWorld())
	{
		DrawDebugSphere(GetWorld(), PreviewAt, BrushRadiusM * 100.0f, 16, Col, false, 0.f, 0, 2.0f);
	}
	if (PreviewMesh)
	{
		PreviewMesh->SetWorldLocation(PreviewAt);
		const float Scale = (BrushRadiusM * 100.0f) / 50.0f;
		PreviewMesh->SetWorldScale3D(FVector(Scale));
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(PreviewMesh->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(Col));
		}
	}
}

void UGXTerrainToolComponent::ApplyTool()
{
	if (!World)
	{
		return;
	}
	const FGXVoxelHit Hit = World->RaycastVoxels(GetTraceStart(), GetTraceDir(), Reach);
	if (!Hit.bHit)
	{
		return;
	}
	if (Mode == EGXToolMode::Drill)
	{
		const FVector Center = BrushCenterFromHit(Hit, GetTraceDir());
		const FGXDigOutcome R = World->DigSphere(Center, BrushRadiusM, DigSpeedMul, RecoveryMul, WearMul);
		if (R.bSuccess && R.MaterialId > 0)
		{
			MaterialStock.FindOrAdd(R.MaterialId) += R.YieldAmount;
		}
	}
	else
	{
		const float Need = BrushRadiusM * BrushRadiusM * BrushRadiusM * 0.5f;
		float& Stock = MaterialStock.FindOrAdd(PlaceMaterialId);
		if (bRequireStockToPlace && Stock < Need * 0.1f)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(21, 1.5f, FColor::Orange, TEXT("No stock to place — dig first"));
			}
			return;
		}
		const FGXDigOutcome R = World->PlaceSphere(Hit.Location, BrushRadiusM, PlaceMaterialId);
		if (R.bSuccess)
		{
			Stock = FMath::Max(0.0f, Stock - Need);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(22, 0.6f, FColor::Cyan, TEXT("Placed"));
			}
		}
	}
}
