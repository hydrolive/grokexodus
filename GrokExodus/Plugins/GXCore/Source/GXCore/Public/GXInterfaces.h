// Copyright Grok Exodus. All Rights Reserved.
// Cross-plugin contracts. GXCelestial must not include GXVoxel headers.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GXInterfaces.generated.h"

UINTERFACE(MinimalAPI)
class UGXVoxelQuery : public UInterface
{
	GENERATED_BODY()
};

/** Density / raycast / deform. Implemented by the voxel world. */
class GXCORE_API IGXVoxelQuery
{
	GENERATED_BODY()

public:
	virtual float SampleDensityMeters(const FVector3d& PlanetLocalMeters) const = 0;
	virtual int32 SampleMaterial(const FVector3d& PlanetLocalMeters) const = 0;
};

UINTERFACE(MinimalAPI)
class UGXGravityField : public UInterface
{
	GENERATED_BODY()
};

/** Inverse-square (+ optional artificial) gravity at a scene position (cm). */
class GXCORE_API IGXGravityField
{
	GENERATED_BODY()

public:
	/** Acceleration in cm/s^2, scene space. */
	virtual FVector GetGravityCmS2(const FVector& ScenePositionCm) const = 0;
	virtual FName GetBodyId() const = 0;
};

UINTERFACE(MinimalAPI)
class UGXAtmosphere : public UInterface
{
	GENERATED_BODY()
};

class GXCORE_API IGXAtmosphere
{
	GENERATED_BODY()

public:
	virtual bool HasAtmosphere() const = 0;
	virtual float GetDensityKgM3(double AltitudeMeters) const = 0;
	virtual double GetAtmosphereHeightMeters() const = 0;
};
