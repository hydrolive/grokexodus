// Copyright Grok Exodus. All Rights Reserved.

#include "GXBootOverlaySubsystem.h"
#include "SGXBootOverlay.h"
#include "GXLoadScreen.h"
#include "GXPresentation.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "GXSkySubsystem.h"
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
		// Stars are depth-tested ISM (M_GXStar). Slate painted over terrain.
		Boot->SetStars(TArray<SGXBootOverlay::FStarDot>());
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
