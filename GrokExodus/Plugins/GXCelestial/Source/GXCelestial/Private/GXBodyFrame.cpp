// Copyright Grok Exodus. All Rights Reserved.

#include "GXBodyFrame.h"

FQuat4d FGXBodyFrame::InertialToBody(const FGXBodyRotation& Rot, double UniversalTime)
{
	const FQuat4d Tilt(FVector3d(1, 0, 0), -Rot.ObliquityRad);
	const FQuat4d Spin(FVector3d(0, 0, 1), Rot.Theta(UniversalTime));
	return (Spin * Tilt).GetNormalized();
}

FVector3d FGXBodyFrame::InertialToBodyPoint(const FQuat4d& R, const FVector3d& InertialM)
{
	return R.RotateVector(InertialM);
}

FVector3d FGXBodyFrame::BodyToInertialPoint(const FQuat4d& R, const FVector3d& BodyM)
{
	return R.Inverse().RotateVector(BodyM);
}

FVector3d FGXBodyFrame::InertialVelocityToBody(
	const FQuat4d& R,
	const FVector3d& OmegaInertial,
	const FVector3d& RInertial,
	const FVector3d& VInertial)
{
	const FVector3d Rel = VInertial - FVector3d::CrossProduct(OmegaInertial, RInertial);
	return R.RotateVector(Rel);
}

FVector3d FGXBodyFrame::BodyVelocityToInertial(
	const FQuat4d& R,
	const FVector3d& OmegaInertial,
	const FVector3d& RInertial,
	const FVector3d& VBody)
{
	return R.Inverse().RotateVector(VBody) + FVector3d::CrossProduct(OmegaInertial, RInertial);
}

double FGXBodyFrame::PointRoundTripError(const FGXBodyRotation& Rot, double UT, const FVector3d& InertialM)
{
	const FQuat4d R = InertialToBody(Rot, UT);
	const FVector3d Body = InertialToBodyPoint(R, InertialM);
	const FVector3d Back = BodyToInertialPoint(R, Body);
	return (Back - InertialM).Size();
}
