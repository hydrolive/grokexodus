// Copyright Grok Exodus. All Rights Reserved.

#include "GXSunLambert.h"
#include "GXCelestial.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UMaterialInstanceDynamic* FGXSunLambert::Apply(UMeshComponent* Mesh, const FLinearColor& Albedo)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Voxel/Materials/M_GXSunLambert.M_GXSunLambert"));
	if (!Parent)
	{
		Parent = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough"));
	}
	if (!Parent)
	{
		UE_LOG(LogGXCelestial, Warning, TEXT("GXSunLambert: no parent material"));
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Parent);
	if (!MID)
	{
		MID = UMaterialInstanceDynamic::Create(Parent, Mesh);
		if (MID)
		{
			Mesh->SetMaterial(0, MID);
		}
	}
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Albedo"), Albedo);
		MID->SetVectorParameterValue(TEXT("Color and Opacity"), Albedo);
		MID->SetVectorParameterValue(TEXT("TintColor"), Albedo);
	}
	return MID;
}

void FGXSunLambert::SetSunDir(UMaterialInstanceDynamic* MID, const FVector3d& SunBodyDir)
{
	if (!MID)
	{
		return;
	}
	const FVector N = FVector(SunBodyDir.X, SunBodyDir.Y, SunBodyDir.Z).GetSafeNormal();
	const FLinearColor C(N.X, N.Y, N.Z, 0.0f);
	MID->SetVectorParameterValue(TEXT("SunDir"), C);
	// Widget3DPassThrough fallback: flat sun-facing gray, never atmosphere red.
	const float Lit = FMath::Clamp(N.Z * 0.15f + 0.55f, 0.12f, 1.0f);
	FLinearColor Albedo;
	if (!MID->GetVectorParameterValue(TEXT("Albedo"), Albedo))
	{
		Albedo = FLinearColor(0.72f, 0.72f, 0.70f);
	}
	const FLinearColor Flat(Albedo.R * Lit, Albedo.G * Lit, Albedo.B * Lit, 1.0f);
	MID->SetVectorParameterValue(TEXT("Color and Opacity"), Flat);
	MID->SetVectorParameterValue(TEXT("TintColor"), Flat);
}
