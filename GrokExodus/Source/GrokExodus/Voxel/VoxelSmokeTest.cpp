// Copyright Epic Games, Inc. All Rights Reserved.

#include "Voxel/VoxelSmokeTest.h"
#include "Voxel/VoxelVolume.h"
#include "Voxel/VoxelPersistence.h"
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace VoxelSmokePrivate
{
	static void Append(FString& Report, const FString& Line)
	{
		Report += Line;
		Report += TEXT("\n");
		UE_LOG(LogVoxelWorld, Log, TEXT("%s"), *Line);
	}
}

bool FVoxelSmokeTest::Run(FString* OutReport)
{
	using namespace VoxelSmokePrivate;

	FString Report;
	int32 Failures = 0;

	auto Expect = [&](bool Cond, const TCHAR* Msg)
	{
		if (Cond)
		{
			Append(Report, FString::Printf(TEXT("[PASS] %s"), Msg));
		}
		else
		{
			++Failures;
			Append(Report, FString::Printf(TEXT("[FAIL] %s"), Msg));
		}
	};

	Append(Report, TEXT("=== Grok Exodus Voxel Phase 0 Smoke Test ==="));

	// Small planet for fast iteration
	FVoxelPlanetParams Params;
	Params.Radius = 64.0f;
	Params.MaxRelief = 8.0f;
	Params.CrustDepth = 4.0f;
	Params.VoxelSize = 1.0f;
	Params.Seed = 42u;

	FVoxelVolume Volume(Params);
	const FVoxelSphereMapping& Map = Volume.GetMapping();

	// --- 1. Spherical density field queries ---
	const FVector Center = FVector::ZeroVector;
	const FVector SurfacePoint(Params.Radius, 0.0f, 0.0f);
	const FVector Outside(Params.Radius + Params.MaxRelief + 20.0f, 0.0f, 0.0f);
	const FVector DeepInside(Params.Radius * 0.5f, 0.0f, 0.0f);

	const float DCenter = Map.SampleDensity(Center);
	const float DDeep = Map.SampleDensity(DeepInside);
	const float DOut = Map.SampleDensity(Outside);

	Expect(DCenter > 0.0f, TEXT("Planet center density is solid (positive)"));
	Expect(DDeep > 0.0f, TEXT("Deep interior density is solid"));
	Expect(DOut < 0.0f, TEXT("Far exterior density is air (negative)"));

	// Surface band: density near zero within relief range
	const float DSurf = Map.SampleDensity(SurfacePoint);
	Expect(FMath::Abs(DSurf) <= Params.MaxRelief + 1.0f, TEXT("Equatorial surface sample within relief band"));

	// Gravity toward center
	const FVector Grav = FVoxelSphereMapping::GravityDirection(SurfacePoint);
	Expect(FVector::DotProduct(Grav, FVector(-1, 0, 0)) > 0.99f, TEXT("Gravity at +X surface points toward -X (center)"));

	// Material assignment non-air for solid
	const FVoxelCell DeepCell = Map.SampleCell(DeepInside);
	Expect(DeepCell.IsSolid() && DeepCell.MaterialId != 0, TEXT("Deep cell has non-air material"));

	// --- 2. Sparse chunk allocate + query override path ---
	const FIntVector SurfVoxel = Map.WorldToVoxel(SurfacePoint);
	const FVoxelChunkCoord SurfChunk = FVoxelSphereMapping::VoxelToChunk(SurfVoxel);
	FVoxelChunk& Chunk = Volume.GetOrCreateChunk(SurfChunk);
	Expect(Volume.GetAllocatedChunkCount() == 1, TEXT("Exactly one chunk allocated after GetOrCreate"));
	Expect(Chunk.GetCells().Num() == FVoxelChunk::CellCount, TEXT("Chunk has 32^3 cells"));

	// --- 3. Deform region (dig a cavity) ---
	FVoxelToolModifiers Tool;
	Tool.DigSpeedMul = 4.0f; // aggressive for test
	Tool.PrecisionMul = 1.0f;
	Tool.RecoveryMul = 1.0f;

	const FVector DigCenter = SurfacePoint - FVector(2.0f, 0.0f, 0.0f); // slightly into crust
	const FVoxelVolume::FBrushResult Brush = Volume.ApplySphereBrush(DigCenter, 3.5f, true, 0, Tool, 1.0f);

	Expect(Brush.VolumeChanged > 0.0f, TEXT("Dig brush removed solid volume"));
	Expect(Brush.DirtyChunks.Num() > 0, TEXT("Dig marked dirty chunks"));

	TArray<FVoxelChunkCoord> DirtyBefore;
	Volume.GetDirtyChunkCoords(DirtyBefore);
	Expect(DirtyBefore.Num() > 0, TEXT("Volume reports dirty chunks after dig"));

	// Verify cavity: center of dig should be air or reduced density
	const FVoxelCell AfterDig = Volume.SampleCell(DigCenter);
	Expect(AfterDig.Density < Map.SampleDensity(DigCenter) || AfterDig.IsAir(),
		TEXT("Edited sample differs from pure procedural / is hollowed"));

	// Place material to seal
	Tool.DigSpeedMul = 1.0f;
	const FVoxelVolume::FBrushResult Place = Volume.ApplySphereBrush(
		DigCenter, 2.0f, false, static_cast<int32>(EVoxelMaterialId::RockyCliff), Tool, 1.0f);
	Expect(Place.VolumeChanged > 0.0f, TEXT("Place brush added volume"));
	const FVoxelCell AfterPlace = Volume.SampleCell(DigCenter);
	Expect(AfterPlace.IsSolid() && AfterPlace.MaterialId == static_cast<int32>(EVoxelMaterialId::RockyCliff),
		TEXT("Placed rocky material at dig center"));

	// Bunker registration
	const FBox BunkerBox(DigCenter - FVector(5), DigCenter + FVector(5));
	Volume.RegisterBunkerVolume(BunkerBox);
	const FVoxelChunk* BunkerChunk = Volume.FindChunk(SurfChunk);
	Expect(BunkerChunk && BunkerChunk->bBunkerResident, TEXT("Bunker volume marks chunk resident"));

	// --- 4. Serialize / deserialize ---
	TArray<uint8> BufferA;
	Expect(FVoxelPersistence::SaveToBuffer(Volume, BufferA), TEXT("SaveToBuffer succeeds"));
	Expect(BufferA.Num() > 64, TEXT("Save buffer non-trivial size"));

	// Fresh volume with same planet params, load deformation
	FVoxelVolume VolumeB(Params);
	Expect(FVoxelPersistence::LoadFromBuffer(VolumeB, BufferA), TEXT("LoadFromBuffer succeeds"));

	const FVoxelCell ReloadCell = VolumeB.SampleCell(DigCenter);
	Expect(ReloadCell.IsSolid() && ReloadCell.MaterialId == AfterPlace.MaterialId,
		TEXT("Reloaded dig-center material matches"));
	Expect(FMath::IsNearlyEqual(ReloadCell.Density, AfterPlace.Density, 0.001f),
		TEXT("Reloaded dig-center density matches"));

	// Round-trip buffer identity
	TArray<uint8> BufferB;
	Expect(FVoxelPersistence::SaveToBuffer(VolumeB, BufferB), TEXT("Re-save after load succeeds"));
	Expect(FVoxelPersistence::BuffersEqual(BufferA, BufferB), TEXT("Serialize round-trip byte-identical"));

	// Craftsmanship hooks sanity
	const float RateSoft = FVoxelMaterialTable::ComputeDigRate(0.5f, Tool);
	const float RateHard = FVoxelMaterialTable::ComputeDigRate(4.0f, Tool);
	Expect(RateSoft > RateHard, TEXT("Softer material digs faster than hard (hardness cascade)"));

	Tool.DigSpeedMul = 2.0f;
	const float RateBoosted = FVoxelMaterialTable::ComputeDigRate(4.0f, Tool);
	Expect(RateBoosted > RateHard, TEXT("Tool DigSpeedMul increases dig rate (craftsmanship hook)"));

	Append(Report, FString::Printf(TEXT("Allocated chunks: %d, payload bytes ~%lld, save bytes: %d"),
		Volume.GetAllocatedChunkCount(),
		Volume.GetAllocatedMemoryBytes(),
		BufferA.Num()));
	Append(Report, FString::Printf(TEXT("=== Result: %s (%d failures) ==="),
		Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), Failures));

	if (OutReport)
	{
		*OutReport = Report;
	}
	return Failures == 0;
}

bool FVoxelSmokeTest::RunWithFileRoundTrip(const FString& Directory)
{
	FVoxelPlanetParams Params;
	Params.Radius = 64.0f;
	Params.MaxRelief = 8.0f;
	Params.Seed = 7u;

	FVoxelVolume Volume(Params);
	const FVector DigCenter(Params.Radius - 2.0f, 0.0f, 0.0f);
	FVoxelToolModifiers Tool;
	Tool.DigSpeedMul = 5.0f;
	Volume.ApplySphereBrush(DigCenter, 4.0f, true, 0, Tool, 1.0f);
	Volume.RegisterBunkerVolume(FBox(DigCenter - FVector(6), DigCenter + FVector(6)));

	const FString Path = FPaths::Combine(Directory, TEXT("phase0_deformed.gxvx"));
	if (!FVoxelPersistence::SaveToFile(Volume, Path))
	{
		return false;
	}

	FVoxelVolume Loaded(Params);
	if (!FVoxelPersistence::LoadFromFile(Loaded, Path))
	{
		return false;
	}

	const FVoxelCell A = Volume.SampleCell(DigCenter);
	const FVoxelCell B = Loaded.SampleCell(DigCenter);
	return A.MaterialId == B.MaterialId && FMath::IsNearlyEqual(A.Density, B.Density, 0.001f);
}

// ---- Automation test ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelPhase0SmokeAutomationTest,
	"GrokExodus.Voxel.Phase0.Smoke",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelPhase0SmokeAutomationTest::RunTest(const FString& Parameters)
{
	FString Report;
	const bool bOk = FVoxelSmokeTest::Run(&Report);
	UE_LOG(LogVoxelWorld, Display, TEXT("\n%s"), *Report);
	TestTrue(TEXT("Phase 0 voxel smoke test"), bOk);
	return bOk;
}

// ---- Console command ----

static FAutoConsoleCommand GVoxelSmokeCmd(
	TEXT("Voxel.SmokeTest"),
	TEXT("Run Phase 0 pure data-structure voxel smoke test (sphere density, edit, serialize)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FString Report;
		const bool bOk = FVoxelSmokeTest::Run(&Report);
		if (bOk)
		{
			const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSmoke"));
			const bool bFile = FVoxelSmokeTest::RunWithFileRoundTrip(Dir);
			UE_LOG(LogVoxelWorld, Display, TEXT("File round-trip: %s (%s)"),
				bFile ? TEXT("PASS") : TEXT("FAIL"), *Dir);
		}
		UE_LOG(LogVoxelWorld, Display, TEXT("Voxel.SmokeTest: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
	}));
