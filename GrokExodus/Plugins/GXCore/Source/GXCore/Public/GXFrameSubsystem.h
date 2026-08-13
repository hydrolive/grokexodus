// Copyright Grok Exodus. All Rights Reserved.
// Active-body frame. The UE scene is body-fixed; cosmology lives in doubles.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GXSnapshot.h"
#include "GXFrameSubsystem.generated.h"

/**
 * One active celestial body is nailed to the world origin.
 * Planets do not translate or rotate as actors.
 * Sky / inertial posing is R_inertial_to_body(UT).
 */
UCLASS()
class GXCORE_API UGXFrameSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "GX|Frame")
	void SetActiveBody(FName BodyId);

	UFUNCTION(BlueprintPure, Category = "GX|Frame")
	FName GetActiveBody() const { return ActiveBodyId; }

	UFUNCTION(BlueprintCallable, Category = "GX|Frame")
	void SetUniversalTime(double Seconds);

	UFUNCTION(BlueprintPure, Category = "GX|Frame")
	double GetUniversalTime() const { return UniversalTime; }

	UFUNCTION(BlueprintCallable, Category = "GX|Frame")
	void AdvanceTime(double DeltaSeconds);

	/** Inertial → body-fixed rotation at the current UT (sky pose). */
	void SetInertialToBody(const FQuat4d& Rotation);
	FQuat4d GetInertialToBody() const { return InertialToBody; }

	/** Body angular velocity in the inertial frame (rad/s). */
	void SetBodyOmegaInertial(const FVector3d& Omega);
	FVector3d GetBodyOmegaInertial() const { return BodyOmegaInertial; }

	/** Scene cm → inertial meters (active body origin). */
	FVector3d SceneToInertialMeters(const FVector& SceneCm) const;

	/** Inertial meters → scene cm. */
	FVector InertialToSceneCm(const FVector3d& InertialMeters) const;

	/** Transform an inertial velocity into the rotating body-fixed scene. */
	FVector3d InertialVelocityToScene(const FVector3d& RInertialM, const FVector3d& VInertialMS) const;

	/** Inverse: scene-relative velocity → inertial. */
	FVector3d SceneVelocityToInertial(const FVector3d& RInertialM, const FVector3d& VSceneMS) const;

	FGXGenerationStamp GetStamp() const { return Stamp; }
	FGXGenerationStamp BumpStamp();

	/** Round-trip identity helper for tests (meters). */
	static bool TransformRoundTripOk(const FQuat4d& R, const FVector3d& InertialM, double Tol = 1e-6);

private:
	UPROPERTY()
	FName ActiveBodyId = NAME_None;

	double UniversalTime = 0.0;
	FQuat4d InertialToBody = FQuat4d::Identity;
	FVector3d BodyOmegaInertial = FVector3d::ZeroVector;
	FGXGenerationStamp Stamp;
};
