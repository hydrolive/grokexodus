// Copyright Grok Exodus. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GXFrameSubsystem.h"
#include "GXJobGraph.h"
#include "GXSaveTypes.h"
#include "GXMath.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCoreFrameRoundTrip, "GX.Core.FrameIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCoreFrameRoundTrip::RunTest(const FString& Parameters)
{
	const FQuat4d R = FQuat4d(FVector3d(0, 0, 1), FMath::DegreesToRadians(37.0));
	const FVector3d P(1234.5, -88.25, 60000.0);
	TestTrue(TEXT("inertial↔body invertibility"), UGXFrameSubsystem::TransformRoundTripOk(R, P, 1e-8));

	const FVector3d Omega(0, 0, 7.27e-5);
	const FVector3d VInertial(0, 767.0, 0);
	const FVector3d Rel = VInertial - FVector3d::CrossProduct(Omega, P);
	const FVector3d SceneV = R.RotateVector(Rel);
	const FVector3d Back = R.Inverse().RotateVector(SceneV) + FVector3d::CrossProduct(Omega, P);
	TestTrue(TEXT("velocity invertibility"), (Back - VInertial).Size() < 1e-6);

	TestEqual(TEXT("save magic"), GXSave::Magic, 0x31535847u);
	TestEqual(TEXT("cm↔m"), GXUnits::MetersToCm3(FVector3d(1, 2, 3)), FVector(100, 200, 300));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXCoreJobStampDiscard, "GX.Core.JobStampDiscard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXCoreJobStampDiscard::RunTest(const FString& Parameters)
{
	FGXJobGraph Graph;
	const FGXGenerationStamp Issued = Graph.GetStamp();

	TAtomic<int32> Ran{ 0 };
	TSharedRef<FGXJobHandle> Handle = Graph.Enqueue(
		EGXJobPriority::NearMesh,
		Issued,
		[&Ran]()
		{
			FPlatformProcess::Sleep(0.02f);
			Ran.IncrementExchange();
		});

	Graph.BumpStamp();
	Graph.Flush(2.0f);

	TestTrue(TEXT("job completed"), Handle->IsDone());
	TestFalse(TEXT("stale result must not apply"), Graph.ShouldApply(Issued));
	TestTrue(TEXT("current stamp applies"), Graph.ShouldApply(Graph.GetStamp()));
	return true;
}
