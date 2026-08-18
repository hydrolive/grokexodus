// Copyright Grok Exodus. All Rights Reserved.

#include "GXBootOverlaySubsystem.h"
#include "SGXBootOverlay.h"
#include "GXLoadScreen.h"
#include "GXPresentation.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "GXSkySubsystem.h"
#include "GXVessel.h"
#include "GXPerf.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SceneView.h"
#include "Stats/Stats.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

void UGXBootOverlaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WriteRunningVersionFile();
	UE_LOG(LogGXPresentation, Warning,
		TEXT("********** GX BUILD %s (%s) boot overlay subsystem init **********"),
		GX_VERSION_STRING, GX_VERSION_DATE);
	TryAttach();
}

void UGXBootOverlaySubsystem::Deinitialize()
{
	RemoveOverlay();
	Super::Deinitialize();
}

void UGXBootOverlaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TryAttach();
}

void UGXBootOverlaySubsystem::Tick(float DeltaTime)
{
	if (!bAttached)
	{
		TryAttach();
	}

	Elapsed += DeltaTime;

	AGXVoxelWorld* WorldActor = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGXVoxelWorld> It(World); It; ++It)
		{
			WorldActor = *It;
			break;
		}
	}

	const bool bReady = WorldActor && WorldActor->IsWorldReady();
	// Never pin the player on the load screen if the world hitch or
	// mesh count never reaches the old near-field quota (0.8.16).
	const bool bForceFade = Elapsed >= (MinHoldSeconds + 4.0f);
	if ((bReady && Elapsed >= MinHoldSeconds) || bForceFade)
	{
		OverlayAlpha = FMath::Max(0.0f, OverlayAlpha - DeltaTime / FMath::Max(FadeSeconds, 0.05f));
	}
	else
	{
		OverlayAlpha = 1.0f;
	}

	if (TSharedPtr<SGXBootOverlay> Boot = StaticCastSharedPtr<SGXBootOverlay>(Overlay))
	{
		const float Progress = WorldActor ? FMath::Clamp(WorldActor->GetLoadProgress(), 0.02f, 1.0f) : 0.05f;
		const FString Status = WorldActor ? WorldActor->GetLoadStatus() : FString(TEXT("Starting planet systems…"));
		FString Extra = TEXT("waiting for AGXVoxelWorld");
		if (WorldActor)
		{
			const FString Toast = WorldActor->GetLastSaveToast();
			Extra = Toast.IsEmpty()
				? FString::Printf(TEXT("%s  %.0f%%"), *Status, Progress * 100.f)
				: Toast;
		}
		Boot->SetState(OverlayAlpha, Progress, Status, Extra);
		FString Flight;
		if (UWorld* World = GetWorld())
		{
			if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
			{
				Flight = Sky->FlightStrip();
			}
		}
		Boot->SetFlight(Flight);

		TArray<SGXBootOverlay::FStarDot> Dots;
		if (OverlayAlpha < 0.20f)
		{
			if (UWorld* World = GetWorld())
			{
				if (UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>())
				{
					if (APlayerController* PC = World->GetFirstPlayerController())
					{
						FVector CamLoc = FVector::ZeroVector;
						FRotator CamRot = FRotator::ZeroRotator;
						PC->GetPlayerViewPoint(CamLoc, CamRot);
						const FVector CamFwd = CamRot.Vector();
						const float PlanetRcm = static_cast<float>(
							(Sky->GetEphemeris().PlanetRadius + 2400.0) * 100.0);
						const FVector3d Sun = Sky->GetSunBodyDir();
						const FVector Up = CamLoc.GetSafeNormal();
						const float SunUp = Up.IsNearlyZero()
							? 1.0f
							: static_cast<float>(Sun.X * Up.X + Sun.Y * Up.Y + Sun.Z * Up.Z);
						const float Night = FMath::Clamp((0.12f - SunUp) / 0.28f, 0.0f, 1.0f);
						FMatrix ViewProj = FMatrix::Identity;
						bool bHaveProj = false;
						if (ULocalPlayer* LP = PC->GetLocalPlayer())
						{
							FSceneViewProjectionData Proj;
							FViewport* VP = (LP->ViewportClient && LP->ViewportClient->Viewport)
								? LP->ViewportClient->Viewport
								: nullptr;
							if (LP->GetProjectionData(VP, Proj, INDEX_NONE))
							{
								ViewProj = Proj.ComputeViewProjectionMatrix();
								bHaveProj = true;
							}
						}
						auto ToUV = [&](const FVector& WorldPos, FVector2f& Out) -> bool
						{
							const FVector4 H = ViewProj.TransformFVector4(FVector4(WorldPos, 1.0f));
							if (H.W <= 0.0f)
							{
								return false;
							}
							const float X = static_cast<float>(H.X / H.W);
							const float Y = static_cast<float>(H.Y / H.W);
							if (X < -1.15f || X > 1.15f || Y < -1.15f || Y > 1.15f)
							{
								return false;
							}
							Out = FVector2f(X * 0.5f + 0.5f, 0.5f - Y * 0.5f);
							return true;
						};
						if (Night > 0.02f && bHaveProj)
						{
							const int32 N = Sky->StarCount();
							Dots.Reserve(N);
							auto HitsSphere = [&](const FVector& Dir, const FVector& Center, float Radius) -> bool
							{
								const FVector L = CamLoc - Center;
								const float B = FVector::DotProduct(L, Dir);
								const float C2 = L.SizeSquared() - Radius * Radius;
								if (C2 <= 0.0f)
								{
									return FVector::DotProduct(Dir, Center - CamLoc) > 0.0f;
								}
								const float Disc = B * B - C2;
								if (Disc < 0.0f)
								{
									return false;
								}
								return (-B - FMath::Sqrt(Disc)) > 0.0f;
							};
							FVector MoonLoc = FVector::ZeroVector;
							float MoonR = 0.0f;
							const bool bMoon = Sky->GetMoonSphere(MoonLoc, MoonR);
							TArray<AGXVessel*> Vessels;
							for (TActorIterator<AGXVessel> It(World); It; ++It)
							{
								if (*It && !It->bBroken)
								{
									Vessels.Add(*It);
								}
							}
							for (int32 I = 0; I < N; ++I)
							{
								const FVector3d Bd = Sky->StarBodyDir(I);
								const FVector Dir(Bd.X, Bd.Y, Bd.Z);
								if (Dir.IsNearlyZero() || FVector::DotProduct(CamFwd, Dir) < 0.10f)
								{
									continue;
								}
								if (HitsSphere(Dir, FVector::ZeroVector, PlanetRcm))
								{
									continue;
								}
								if (bMoon && HitsSphere(Dir, MoonLoc, MoonR * 1.12f))
								{
									continue;
								}
								bool bHitVessel = false;
								for (AGXVessel* Ves : Vessels)
								{
									const FVector S = Ves->GetActorScale3D();
									const float VR = 50.0f * FMath::Max3(FMath::Abs(S.X), FMath::Abs(S.Y), FMath::Abs(S.Z)) * 1.15f;
									if (HitsSphere(Dir, Ves->GetActorLocation(), VR))
									{
										bHitVessel = true;
										break;
									}
								}
								if (bHitVessel)
								{
									continue;
								}
								FVector2f UV;
								if (!ToUV(CamLoc + Dir * 8.0e7f, UV))
								{
									continue;
								}
								const float Mag = Sky->StarMagnitude(I);
								SGXBootOverlay::FStarDot Dot;
								Dot.UV = UV;
								Dot.SizePx = FMath::Clamp(5.4f - Mag * 0.50f, 2.0f, 6.5f);
								Dot.Alpha = Night * FMath::Clamp(1.10f - Mag * 0.14f, 0.28f, 1.0f);
								Dots.Add(Dot);
							}
							static double LastStarLog = -1.0e9;
							const double Now = FPlatformTime::Seconds();
							if (Now - LastStarLog > 2.0)
							{
								LastStarLog = Now;
								GX_PERF(1, TEXT("GX-stars slate n=%d night=%.2f sunUp=%.2f"),
									Dots.Num(), Night, SunUp);
							}
						}
					}
				}
			}
		}
		Boot->SetStars(Dots);
	}
}

TStatId UGXBootOverlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGXBootOverlaySubsystem, STATGROUP_Tickables);
}

bool UGXBootOverlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UGXBootOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UGXBootOverlaySubsystem::TryAttach()
{
	if (bAttached && Overlay.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport && GEngine)
	{
		Viewport = GEngine->GameViewport;
	}
	if (!Viewport)
	{
		return;
	}

	const TSharedRef<SGXBootOverlay> Boot = SNew(SGXBootOverlay);
	Overlay = Boot;
	Viewport->AddViewportWidgetContent(Boot, 10000);
	HostViewport = Viewport;
	bAttached = true;
	GXLoadScreen::SetSlateOverlayActive(true);

	UE_LOG(LogGXPresentation, Warning,
		TEXT("********** GX BUILD %s overlay attached (Slate viewport, independent of HUD) **********"),
		GX_VERSION_STRING);
	WriteRunningVersionFile();
}

void UGXBootOverlaySubsystem::RemoveOverlay()
{
	if (Overlay.IsValid() && HostViewport.IsValid())
	{
		HostViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	HostViewport.Reset();
	bAttached = false;
	GXLoadScreen::SetSlateOverlayActive(false);
}

void UGXBootOverlaySubsystem::WriteRunningVersionFile() const
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("GX_RUNNING_VERSION.txt");
	const FString Body = FString::Printf(
		TEXT("GX %s\n%s\nmodule=GXPresentation\nslate_overlay=1\n"),
		GX_VERSION_STRING, GX_VERSION_DATE);
	FFileHelper::SaveStringToFile(Body, *Path);
}
