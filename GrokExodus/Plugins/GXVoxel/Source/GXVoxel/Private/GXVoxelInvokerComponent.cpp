// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxelInvokerComponent.h"

UGXVoxelInvokerComponent::UGXVoxelInvokerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVector UGXVoxelInvokerComponent::GetInvokerWorldLocation() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorLocation();
	}
	return FVector::ZeroVector;
}
