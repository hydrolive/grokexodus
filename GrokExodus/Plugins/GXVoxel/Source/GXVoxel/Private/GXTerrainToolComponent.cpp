// Copyright Grok Exodus. All Rights Reserved.

#include "GXTerrainToolComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

UGXTerrainToolComponent::UGXTerrainToolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGXTerrainToolComponent::BeginPlay()
{
	Super::BeginPlay();
	TryFindWorld();
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
		FireCooldown = 0.08f;
	}

	if (bDrawDebugPreview && World && GetWorld())
	{
		const FGXVoxelHit Hit = World->RaycastVoxels(GetTraceStart(), GetTraceDir(), Reach);
		if (Hit.bHit)
		{
			DrawDebugSphere(GetWorld(), Hit.Location, BrushRadiusM * 100.0f, 8,
				Mode == EGXToolMode::Drill ? FColor::Orange : FColor::Cyan, false, 0.f, 0, 1.5f);
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
		const FGXDigOutcome R = World->DigSphere(Hit.Location, BrushRadiusM, DigSpeedMul, RecoveryMul, WearMul);
		if (R.bSuccess && R.MaterialId > 0)
		{
			MaterialStock.FindOrAdd(R.MaterialId) += R.YieldAmount;
		}
	}
	else
	{
		const float Need = BrushRadiusM * BrushRadiusM * BrushRadiusM * 0.5f;
		float& Stock = MaterialStock.FindOrAdd(PlaceMaterialId);
		if (Stock < Need * 0.1f)
		{
			return;
		}
		const FGXDigOutcome R = World->PlaceSphere(Hit.Location + Hit.Normal * 20.0f, BrushRadiusM, PlaceMaterialId);
		if (R.bSuccess)
		{
			Stock = FMath::Max(0.0f, Stock - Need);
		}
	}
}
