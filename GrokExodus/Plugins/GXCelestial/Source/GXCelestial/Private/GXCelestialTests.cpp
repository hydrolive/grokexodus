// Copyright Grok Exodus. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GXKepler.h"
#include "GXGravity.h"
#include "GXBodyFrame.h"
#include "GXEphemeris.h"
#include "GXSkySubsystem.h"
#include "GXFrameSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCelestialClosedOrbit, "GX.Celestial.ClosedOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCelestialClosedOrbit::RunTest(const FString& Parameters)
{
	const double R = 60000.0 + 20000.0; // 20 km alt
	const double Mu = 9.81 * 60000.0 * 60000.0;
	FGXKeplerElements E;
	E.SemiMajorAxis = R;
	E.Eccentricity = 0.0;
	E.Mu = Mu;
	const double Err = FGXKepler::ClosedOrbitError(E, 10);
	TestTrue(TEXT("10 periods close within 1 m"), Err < 1.0);

	const double V = FGXKepler::CircularVelocity(Mu, 60000.0);
	TestTrue(TEXT("surface circ ~767"), FMath::Abs(V - 767.0) < 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCelestialEciBody, "GX.Celestial.EciBodyInvertible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCelestialEciBody::RunTest(const FString& Parameters)
{
	FGXBodyRotation Rot;
	Rot.SiderealPeriod = 1440.0;
	Rot.ObliquityRad = FMath::DegreesToRadians(23.0);
	const FVector3d P(60000.0, 1500.0, -800.0);
	const double Err = FGXBodyFrame::PointRoundTripError(Rot, 333.0, P);
	TestTrue(TEXT("point invertibility"), Err < 1e-6);

	const FQuat4d Q = FGXBodyFrame::InertialToBody(Rot, 333.0);
	const FVector3d Omega(0, 0, Rot.Omega());
	const FVector3d Vin(0, 767.0, 10.0);
	const FVector3d Vbody = FGXBodyFrame::InertialVelocityToBody(Q, Omega, P, Vin);
	const FVector3d Back = FGXBodyFrame::BodyVelocityToInertial(Q, Omega, P, Vbody);
	TestTrue(TEXT("velocity invertibility"), (Back - Vin).Size() < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCelestialAtmoHeat, "GX.Celestial.DragAndHeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCelestialAtmoHeat::RunTest(const FString& Parameters)
{
	FGXAtmosphereModel A;
	TestTrue(TEXT("vacuum above atmo"), A.DensityAt(20000.0) == 0.0f);
	TestTrue(TEXT("SL density"), A.DensityAt(0.0) > 1.0f);

	const double Slow = FGXGravity::HeatFlux(0.1, 100.0);
	const double Fast = FGXGravity::HeatFlux(0.1, 800.0);
	TestTrue(TEXT("heat grows with v"), Fast > Slow * 100.0);

	const FVector3d G = FGXGravity::Acceleration(FVector3d(60000, 0, 0), 9.81 * 60000.0 * 60000.0);
	TestTrue(TEXT("surface g ~9.81"), FMath::Abs(G.Size() - 9.81) < 1e-3);
	TestTrue(TEXT("toward center"), G.X < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCelestialSkyNoon, "GX.Celestial.SkyNoon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCelestialSkyNoon::RunTest(const FString& Parameters)
{
	const FGXEphemeris E = FGXEphemeris::PlayableEarth();
	const FVector3d Sun0 = E.SunBodyDir(0.0);
	TestTrue(TEXT("UT0 noon on +X"), Sun0.X > 0.70);
	const FVector3d SunNight = E.SunBodyDir(E.EarthRot.SiderealPeriod * 0.5);
	TestTrue(TEXT("half-day is night on +X"), SunNight.X < 0.0);
	const FQuat4d R = E.InertialToBody(111.0);
	TestTrue(TEXT("frame invert"), UGXFrameSubsystem::TransformRoundTripOk(R, FVector3d(60000, 40, -20)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCelestialWarpRefuse, "GX.Celestial.WarpRefuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCelestialWarpRefuse::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("vacuum ok"), !UGXSkySubsystem::ShouldRefusePhysicsWarp(0.0, false));
	TestTrue(TEXT("atmo refuse"), UGXSkySubsystem::ShouldRefusePhysicsWarp(0.2, false));
	TestTrue(TEXT("thrust refuse"), UGXSkySubsystem::ShouldRefusePhysicsWarp(0.0, true));
	return true;
}
