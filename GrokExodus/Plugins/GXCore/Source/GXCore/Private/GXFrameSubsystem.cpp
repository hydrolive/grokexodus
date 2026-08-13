// Copyright Grok Exodus. All Rights Reserved.

#include "GXFrameSubsystem.h"
#include "GXCore.h"
#include "GXMath.h"

void UGXFrameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Stamp.Value = 1;
	UE_LOG(LogGXCore, Log, TEXT("GXFrameSubsystem initialized"));
}

void UGXFrameSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UGXFrameSubsystem::SetActiveBody(FName BodyId)
{
	if (ActiveBodyId == BodyId)
	{
		return;
	}
	ActiveBodyId = BodyId;
	BumpStamp();
}

void UGXFrameSubsystem::SetUniversalTime(double Seconds)
{
	UniversalTime = Seconds;
}

void UGXFrameSubsystem::AdvanceTime(double DeltaSeconds)
{
	UniversalTime += DeltaSeconds;
}

void UGXFrameSubsystem::SetInertialToBody(const FQuat4d& Rotation)
{
	InertialToBody = Rotation.GetNormalized();
}

void UGXFrameSubsystem::SetBodyOmegaInertial(const FVector3d& Omega)
{
	BodyOmegaInertial = Omega;
}

FVector3d UGXFrameSubsystem::SceneToInertialMeters(const FVector& SceneCm) const
{
	const FVector3d BodyM = GXUnits::CmToMeters3(SceneCm);
	return InertialToBody.Inverse().RotateVector(BodyM);
}

FVector UGXFrameSubsystem::InertialToSceneCm(const FVector3d& InertialMeters) const
{
	const FVector3d BodyM = InertialToBody.RotateVector(InertialMeters);
	return GXUnits::MetersToCm3(BodyM);
}

FVector3d UGXFrameSubsystem::InertialVelocityToScene(const FVector3d& RInertialM, const FVector3d& VInertialMS) const
{
	// v_body = R * (v_inertial − ω × r_inertial)
	const FVector3d Rel = VInertialMS - FVector3d::CrossProduct(BodyOmegaInertial, RInertialM);
	return InertialToBody.RotateVector(Rel);
}

FVector3d UGXFrameSubsystem::SceneVelocityToInertial(const FVector3d& RInertialM, const FVector3d& VSceneMS) const
{
	const FVector3d RelInertial = InertialToBody.Inverse().RotateVector(VSceneMS);
	return RelInertial + FVector3d::CrossProduct(BodyOmegaInertial, RInertialM);
}

FGXGenerationStamp UGXFrameSubsystem::BumpStamp()
{
	++Stamp.Value;
	if (Stamp.Value == 0)
	{
		Stamp.Value = 1;
	}
	return Stamp;
}

bool UGXFrameSubsystem::TransformRoundTripOk(const FQuat4d& R, const FVector3d& InertialM, double Tol)
{
	const FQuat4d Q = R.GetNormalized();
	const FVector3d Body = Q.RotateVector(InertialM);
	const FVector3d Back = Q.Inverse().RotateVector(Body);
	return (Back - InertialM).Size() <= Tol;
}
