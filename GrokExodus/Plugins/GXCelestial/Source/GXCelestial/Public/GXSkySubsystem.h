// Copyright Grok Exodus. All Rights Reserved.
// Wave C: ephemeris sky. The planet actor does not move.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GXEphemeris.h"
#include "GXSkySubsystem.generated.h"

class ADirectionalLight;
class AStaticMeshActor;
class AGXVessel;

/**
 * Owns UT, inertial→body, the sun lamp, Moon impostor, and time warp.
 * Kepler lives in doubles. Directional light and impostors are posed each tick.
 */
UCLASS()
class GXCELESTIAL_API UGXSkySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	const FGXEphemeris& GetEphemeris() const { return Eph; }
	double GetUniversalTime() const { return UniversalTime; }
	double GetWarp() const;
	int32 GetWarpIndex() const { return WarpIndex; }
	bool WasWarpRefused() const { return bWarpRefused; }
	FVector3d GetSunBodyDir() const { return LastSunBody; }
	FVector3d GetMoonBodyDir() const { return LastMoonBody; }
	AGXVessel* GetFollowedVessel() const;
	int32 GetFollowIndex() const { return FollowIndex; }

	/** Star unit vector in the body-fixed scene. */
	FVector3d StarBodyDir(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void SetWarpIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void StepWarp(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void SetUniversalTime(double Seconds);

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void CycleFollow();

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void ClearFollow();

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	void JumpToSeason(int32 SeasonIndex);

	UFUNCTION(BlueprintCallable, Category = "GX|Sky")
	bool ToggleParachuteOnFollowed();

	void AddFollowOrbit(float YawDeg, float PitchDeg);
	void AddFollowZoom(float WheelSteps);

	/** Physics warp is refused in atmosphere or while thrusting. */
	static bool ShouldRefusePhysicsWarp(double DensityKgM3, bool bThrusting);

	FString FlightStrip() const;

	static constexpr int32 WarpCount = 7;
	static const double WarpSteps[WarpCount];

private:
	void BindConsole();
	void UnbindConsole();
	void EnsureActors();
	void PoseSun();
	void PoseMoon();
	void SyncFrame() const;
	void SpawnDemoVessel();
	AGXVessel* FindFollowedVessel() const;
	void ApplyFollowView();
	void CollectVessels(TArray<AGXVessel*>& Out) const;
	void EnsureStarField();
	void UpdateStarField();

	FGXEphemeris Eph;
	double UniversalTime = 0.0;
	int32 WarpIndex = 0;
	int32 FollowIndex = -1;
	float FollowYawDeg = 0.0f;
	float FollowPitchDeg = 18.0f;
	float FollowDistM = 220.0f;
	bool bWarpRefused = false;
	bool bActorsReady = false;
	bool bDemoSpawned = false;
	FVector3d LastSunBody = FVector3d(1, 0, 0);
	FVector3d LastMoonBody = FVector3d(0, 1, 0);
	float SkyMs = 0.0f;

	TWeakObjectPtr<ADirectionalLight> SunLight;
	TWeakObjectPtr<AStaticMeshActor> MoonImpostor;
	TWeakObjectPtr<AGXVessel> DemoVessel;
	TWeakObjectPtr<class UInstancedStaticMeshComponent> StarISM;

	IConsoleObject* CmdWarp = nullptr;
	IConsoleObject* CmdDump = nullptr;
	IConsoleObject* CmdSpawn = nullptr;
	IConsoleObject* CmdFollow = nullptr;
	IConsoleObject* CmdChute = nullptr;
	IConsoleObject* CmdSeason = nullptr;
};
