// Copyright Grok Exodus. All Rights Reserved.
// Bright-star catalog in Earth-centered inertial (equatorial).
#pragma once

#include "CoreMinimal.h"

struct FGXStar
{
	const TCHAR* Name = TEXT("");
	float RaHours = 0.f;
	float DecDeg = 0.f;
	float Mag = 0.f;

	FVector3d InertialDir() const
	{
		const double Ra = static_cast<double>(RaHours) * (PI / 12.0);
		const double Dec = FMath::DegreesToRadians(static_cast<double>(DecDeg));
		const double C = FMath::Cos(Dec);
		return FVector3d(C * FMath::Cos(Ra), C * FMath::Sin(Ra), FMath::Sin(Dec));
	}
};

struct GXCELESTIAL_API FGXStarCatalog
{
	static constexpr int32 Count = 40;
	static constexpr int32 FieldCount = 320;
	static constexpr int32 TotalCount = Count + FieldCount;
	static const FGXStar Stars[Count];

	static FVector3d Dir(int32 Index);
	static float Magnitude(int32 Index);
};
