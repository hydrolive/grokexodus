// Copyright Grok Exodus. All Rights Reserved.

#include "GXHUDLayout.h"
#include "GXLoadScreen.h"
#include "GXVersion.h"
#include "GXVoxelWorld.h"
#include "GXSkySubsystem.h"
#include "GXStarCatalog.h"
#include "GXVessel.h"
#include "GXFrameSubsystem.h"
#include "GXKepler.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AGXHUDLayout::AGXHUDLayout()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AGXHUDLayout::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
}

void AGXHUDLayout::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || GXLoadScreen::IsSlateOverlayActive())
	{
		return;
	}

	if (UWorld* W = GetWorld())
	{
		Elapsed += W->GetDeltaSeconds();
	}

	AGXVoxelWorld* WorldActor = nullptr;
	if (GetWorld())
	{
		for (TActorIterator<AGXVoxelWorld> It(GetWorld()); It; ++It)
		{
			WorldActor = *It;
			break;
		}
	}

	const bool bReady = WorldActor && WorldActor->IsWorldReady();
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	OverlayAlpha = GXLoadScreen::Draw(this, Canvas, OverlayAlpha, bReady, MinHoldSeconds, Elapsed, FadeSeconds, Dt, WorldActor);

	const FString Extra = WorldActor
		? FString::Printf(TEXT("%s  %.0f%%"), *WorldActor->GetLoadStatus(), WorldActor->GetLoadProgress() * 100.f)
		: FString(TEXT("no AGXVoxelWorld"));
	GXLoadScreen::DrawVersionStrip(Canvas, Extra);

	if (OverlayAlpha <= 0.01f && bDrawDebugStrip && GEngine && GEngine->GetSmallFont())
	{
		Canvas->SetDrawColor(FColor(220, 230, 240));
		Canvas->DrawText(GEngine->GetSmallFont(),
			TEXT("LMB drill  |  RMB mode  |  F5 save  |  ,/. warp  |  V follow  |  P chute"),
			16.f, 36.f, 1.1f, 1.1f);
		DrawFlightInstruments();
	}
}

void AGXHUDLayout::DrawFlightInstruments()
{
	if (!Canvas || !GEngine || !GEngine->GetSmallFont())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UGXSkySubsystem* Sky = World->GetSubsystem<UGXSkySubsystem>();
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Pawn)
	{
		return;
	}

	const FVector Loc = Pawn->GetActorLocation();
	const FVector Up = Loc.GetSafeNormal();
	FVector East = FVector::CrossProduct(FVector(0, 0, 1), Up);
	if (East.IsNearlyZero())
	{
		East = FVector::CrossProduct(FVector(0, 1, 0), Up);
	}
	East.Normalize();
	const FVector North = FVector::CrossProduct(Up, East).GetSafeNormal();
	const FVector Vel = Pawn->GetVelocity();
	const float SpdMs = Vel.Size() * 0.01f;
	const float AltM = Loc.Size() * 0.01f - (Sky ? Sky->GetEphemeris().PlanetRadius : 60000.f);

	FVector Look = Pawn->GetActorForwardVector();
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			Look = PC->PlayerCameraManager->GetCameraRotation().Vector();
		}
	}
	const FVector LookHoriz = FVector::VectorPlaneProject(Look, Up).GetSafeNormal();
	float Heading = 0.f;
	if (!LookHoriz.IsNearlyZero())
	{
		Heading = FMath::RadiansToDegrees(FMath::Atan2(
			FVector::DotProduct(LookHoriz, East),
			FVector::DotProduct(LookHoriz, North)));
		if (Heading < 0.f)
		{
			Heading += 360.f;
		}
	}
	const float Pitch = FMath::RadiansToDegrees(FMath::Asin(
		FMath::Clamp(FVector::DotProduct(Look, Up), -1.f, 1.f)));

	const float Cx = Canvas->SizeX * 0.5f;
	const float Cy = Canvas->SizeY - 110.f;
	const float R = 72.f;
	Canvas->K2_DrawBox(FVector2D(Cx - R, Cy - R), FVector2D(R * 2.f, R * 2.f), 1.5f, FLinearColor(0.15f, 0.18f, 0.22f, 0.55f));

	auto Ring = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Col)
	{
		Canvas->K2_DrawLine(A, B, 1.4f, Col);
	};
	const int32 Segs = 48;
	for (int32 I = 0; I < Segs; ++I)
	{
		const float A0 = 2.f * PI * I / Segs;
		const float A1 = 2.f * PI * (I + 1) / Segs;
		Ring(FVector2D(Cx + R * FMath::Cos(A0), Cy + R * FMath::Sin(A0)),
			FVector2D(Cx + R * FMath::Cos(A1), Cy + R * FMath::Sin(A1)),
			FLinearColor(0.75f, 0.82f, 0.88f, 0.85f));
	}
	Ring(FVector2D(Cx - R + 6.f, Cy), FVector2D(Cx + R - 6.f, Cy), FLinearColor(0.55f, 0.62f, 0.55f, 0.8f));

	auto Marker = [&](const FVector& Dir, const FLinearColor& Col, float Size)
	{
		const float X = FVector::DotProduct(Dir, East);
		const float Y = FVector::DotProduct(Dir, North);
		const float Z = FVector::DotProduct(Dir, Up);
		if (Z < -0.15f)
		{
			return;
		}
		const float S = R * 0.82f;
		const FVector2D P(Cx + X * S, Cy - Y * S);
		Canvas->K2_DrawBox(P - FVector2D(Size, Size), FVector2D(Size * 2.f, Size * 2.f), 1.6f, Col);
	};
	if (!Vel.IsNearlyZero())
	{
		Marker(Vel.GetSafeNormal(), FLinearColor(0.95f, 0.35f, 0.15f, 0.95f), 4.f);
	}
	if (Sky)
	{
		const FVector3d Sd = Sky->GetSunBodyDir();
		Marker(FVector(Sd.X, Sd.Y, Sd.Z), FLinearColor(1.f, 0.86f, 0.25f, 0.95f), 3.f);
		const FVector3d Md = Sky->GetMoonBodyDir();
		Marker(FVector(Md.X, Md.Y, Md.Z), FLinearColor(0.75f, 0.78f, 0.85f, 0.9f), 3.f);
	}
	Canvas->SetDrawColor(FColor(230, 235, 240));
	Canvas->DrawText(GEngine->GetSmallFont(),
		FString::Printf(TEXT("HDG %.0f  PIT %+.0f"), Heading, Pitch),
		Cx - 58.f, Cy + R + 6.f, 1.05f, 1.05f);

	FString Orbit = FString::Printf(TEXT("ALT %+.0f m   SPD %.1f m/s"), AltM, SpdMs);
	if (Sky)
	{
		Orbit += TEXT("\n") + Sky->FlightStrip();
	}
	if (UGXFrameSubsystem* Frame = World->GetSubsystem<UGXFrameSubsystem>())
	{
		const FVector3d Ri = Frame->SceneToInertialMeters(Loc);
		const FVector3d Vi = Frame->SceneVelocityToInertial(Ri, FVector3d(Vel.X, Vel.Y, Vel.Z) * 0.01);
		if (Vi.Size() > 80.0)
		{
			const double Mu = Sky ? Sky->GetEphemeris().PlanetMu : 3.5316e10;
			const FGXKeplerElements El = FGXKepler::FromState(Ri, Vi, Mu, Sky ? Sky->GetUniversalTime() : 0.0);
			if (El.Eccentricity < 1.0 && El.SemiMajorAxis > 1000.0)
			{
				const double Pe = El.SemiMajorAxis * (1.0 - El.Eccentricity) - (Sky ? Sky->GetEphemeris().PlanetRadius : 60000.0);
				const double Ap = El.SemiMajorAxis * (1.0 + El.Eccentricity) - (Sky ? Sky->GetEphemeris().PlanetRadius : 60000.0);
				Orbit += FString::Printf(TEXT("\nPe %+.0f  Ap %+.0f"), Pe, Ap);
			}
		}
	}
	for (TActorIterator<AGXVessel> It(World); It; ++It)
	{
		Orbit += TEXT("\n") + It->StatusLine();
	}

	const float Ox = Canvas->SizeX - 360.f;
	const float Oy = 52.f;
	Canvas->K2_DrawBox(FVector2D(Ox - 8.f, Oy - 6.f), FVector2D(350.f, 92.f), 1.f, FLinearColor(0.08f, 0.09f, 0.11f, 0.45f));
	Canvas->SetDrawColor(FColor(210, 230, 245));
	TArray<FString> Lines;
	Orbit.ParseIntoArrayLines(Lines);
	float Ty = Oy;
	for (const FString& Line : Lines)
	{
		Canvas->DrawText(GEngine->GetSmallFont(), Line, Ox, Ty, 1.05f, 1.05f);
		Ty += 16.f;
	}

	// Read-only polar map: +Z north, player + vessels.
	const float Mx = 70.f;
	const float My = Canvas->SizeY - 90.f;
	const float Mr = 48.f;
	for (int32 I = 0; I < Segs; ++I)
	{
		const float A0 = 2.f * PI * I / Segs;
		const float A1 = 2.f * PI * (I + 1) / Segs;
		Ring(FVector2D(Mx + Mr * FMath::Cos(A0), My + Mr * FMath::Sin(A0)),
			FVector2D(Mx + Mr * FMath::Cos(A1), My + Mr * FMath::Sin(A1)),
			FLinearColor(0.45f, 0.7f, 0.85f, 0.8f));
	}
	auto MapDot = [&](const FVector& P, const FLinearColor& Col, float Size)
	{
		const FVector N = P.GetSafeNormal();
		const FVector2D D(Mx + static_cast<float>(N.Y) * Mr, My - static_cast<float>(N.X) * Mr);
		Canvas->K2_DrawBox(D - FVector2D(Size, Size), FVector2D(Size * 2.f, Size * 2.f), 1.5f, Col);
	};
	MapDot(Loc, FLinearColor(0.3f, 1.f, 0.4f, 1.f), 3.f);
	if (Sky)
	{
		const FVector3d Sd = Sky->GetSunBodyDir();
		MapDot(FVector(Sd.X, Sd.Y, Sd.Z) * 1000.f, FLinearColor(1.f, 0.85f, 0.2f, 1.f), 2.f);
	}
	for (TActorIterator<AGXVessel> It(World); It; ++It)
	{
		MapDot(It->GetActorLocation(), FLinearColor(1.f, 0.4f, 0.2f, 1.f), 2.f);
	}
	Canvas->SetDrawColor(FColor(180, 200, 210));
	Canvas->DrawText(GEngine->GetSmallFont(), TEXT("MAP"), Mx - 14.f, My + Mr + 4.f, 0.9f, 0.9f);

	if (Sky)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			return;
		}
		FVector CamLoc = Loc;
		FVector CamLook = Look;
		if (PC && PC->PlayerCameraManager)
		{
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
			CamLook = PC->PlayerCameraManager->GetCameraRotation().Vector();
		}
		const FVector LocalUp = CamLoc.GetSafeNormal();
		const bool bDay = FVector::DotProduct(
			FVector(Sky->GetSunBodyDir().X, Sky->GetSunBodyDir().Y, Sky->GetSunBodyDir().Z),
			LocalUp) > 0.08f;
		const float MagCut = bDay ? 0.2f : 2.0f;
		for (int32 I = 0; I < FGXStarCatalog::Count; ++I)
		{
			if (FGXStarCatalog::Stars[I].Mag > MagCut)
			{
				continue;
			}
			const FVector3d Bd = Sky->StarBodyDir(I);
			const FVector Dir(Bd.X, Bd.Y, Bd.Z);
			if (FVector::DotProduct(Dir, LocalUp) < 0.04f || FVector::DotProduct(Dir, CamLook) < 0.18f)
			{
				continue;
			}
			FVector2D Sp;
			const FVector WorldPt = CamLoc + Dir * 80000000.f;
			if (!UGameplayStatics::ProjectWorldToScreen(PC, WorldPt, Sp, false))
			{
				continue;
			}
			const float Size = FMath::Clamp(2.6f - FGXStarCatalog::Stars[I].Mag * 0.55f, 1.2f, 3.4f);
			const float A = bDay ? 0.25f : 0.9f;
			Canvas->K2_DrawBox(Sp - FVector2D(Size, Size), FVector2D(Size * 2.f, Size * 2.f), 1.2f,
				FLinearColor(0.92f, 0.94f, 1.f, A));
		}
		if (AGXVessel* Follow = Sky->GetFollowedVessel())
		{
			Canvas->SetDrawColor(FColor(255, 200, 80));
			Canvas->DrawText(GEngine->GetSmallFont(),
				FString::Printf(TEXT("FOLLOW %s"), *Follow->GetName()),
				16.f, 54.f, 1.1f, 1.1f);
		}
	}
}
