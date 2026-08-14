// Copyright Grok Exodus. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GXVoxelVolume.h"
#include "GXMesher.h"
#include "GXNoise.h"
#include "GXVoxelStamps.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXVoxelDensityIdentity, "GX.Voxel.DensityIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXVoxelDensityIdentity::RunTest(const FString& Parameters)
{
	// Same algorithm as FVoxelSphereMapping (legacy 4 km prototype params).
	const FGXPlanetStampParams Params = FGXPlanetStampParams::LegacyPrototype();
	const FGXSphereStamp Stamp(Params);
	FGXVoxelVolume Volume(Params);

	int32 Mismatches = 0;
	double MaxAbsErr = 0.0;
	for (int32 I = 0; I < 256; ++I)
	{
		const float T = static_cast<float>(I) / 255.0f;
		const float Lon = T * 2.0f * PI;
		const float Lat = (T - 0.5f) * PI * 0.9f;
		const FVector3d Dir(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
		const FVector3d P = Dir * static_cast<double>(Params.Radius + 10.0f);
		const float A = Stamp.SampleDensity(P);
		const float B = Volume.SampleDensity(P);
		MaxAbsErr = FMath::Max(MaxAbsErr, static_cast<double>(FMath::Abs(A - B)));
		if (FMath::Abs(A - B) > 1e-3f)
		{
			++Mismatches;
		}
	}
	TestEqual(TEXT("unedited volume matches stamp"), Mismatches, 0);
	TestTrue(TEXT("max abs err < 1mm after quantize on unedited? unedited is full float via stamp"), MaxAbsErr < 1e-4);

	// Packed quantization round-trip
	const FGXVoxelPacked Packed = FGXVoxelPacked::FromDensity(3.25f, 2, 0);
	TestTrue(TEXT("quantize ~1mm"), FMath::Abs(Packed.ToDensityMeters() - 3.25f) < 0.002f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXVoxelPageSparse, "GX.Voxel.PageSparseRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXVoxelPageSparse::RunTest(const FString& Parameters)
{
	FGXVoxelVolume Volume(FGXPlanetStampParams::LegacyPrototype());
	TestEqual(TEXT("no pages until edit"), Volume.GetAllocatedPageCount(), 0);

	const FIntVector Coord(4010, 0, 0);
	const FGXVoxelPacked Air = FGXVoxelPacked::MakeAir();
	Volume.SetVoxel(Coord, Air);

	TestEqual(TEXT("one page allocated"), Volume.GetAllocatedPageCount(), 1);
	TestTrue(TEXT("page << dense chunk"), Volume.GetAllocatedBytes() < 32 * 32 * 32 * 4);

	const float VoxelSize = Volume.GetStamp().GetParams().VoxelSize;
	const FVector3d Center(
		(static_cast<double>(Coord.X) + 0.5) * VoxelSize,
		(static_cast<double>(Coord.Y) + 0.5) * VoxelSize,
		(static_cast<double>(Coord.Z) + 0.5) * VoxelSize);
	TestTrue(TEXT("edit visible"), Volume.Sample(Center).IsSolid() == false);

	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume.PublishSnapshot();
	TestTrue(TEXT("snapshot sees edit"), Snap->Sample(Center).IsSolid() == false);
	TestEqual(TEXT("snapshot stamp"), Snap->Stamp.Value, Volume.GetStampValue().Value);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXVoxelMeshWatertight, "GX.Voxel.MeshSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXVoxelMeshWatertight::RunTest(const FString& Parameters)
{
	FGXPlanetStampParams P = FGXPlanetStampParams::LegacyPrototype();
	P.Radius = 64.0f;
	P.MaxRelief = 0.0f;
	FGXVoxelVolume Volume(P);
	TSharedRef<FGXVoxelSnapshot, ESPMode::ThreadSafe> Snap = Volume.PublishSnapshot();
	const FGXChunkKey Key = FGXVoxelVolume::VoxelToChunk(FIntVector(64, 0, 0));
	const FGXMeshBuffers Mesh = FGXMesher::MeshChunk(*Snap, Key, FGXMesher::FSettings());
	TestTrue(TEXT("mesh has tris"), Mesh.Indices.Num() >= 3 && (Mesh.Indices.Num() % 3) == 0);
	TestTrue(TEXT("mesh has verts"), Mesh.Positions.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXVoxelEarthGeomorphology, "GX.Voxel.EarthGeomorphology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXVoxelEarthGeomorphology::RunTest(const FString& Parameters)
{
	const FGXPlanetStampParams Params = FGXPlanetStampParams::Earth();
	TestEqual(TEXT("Earth profile"), static_cast<int32>(Params.Profile), static_cast<int32>(EGXStampProfile::Earth));
	TestTrue(TEXT("60 km radius"), FMath::IsNearlyEqual(Params.Radius, 60000.0f));
	TestTrue(TEXT("km-scale relief"), Params.MaxRelief >= 2000.0f);

	const FGXSphereStamp Stamp(Params);
	const FVector3f PlusX(1, 0, 0);
	const float SpawnH = Stamp.SampleHeightDisplacement(PlusX);
	TestTrue(TEXT("+X spawn is land"), SpawnH > 20.0f);

	const FVector3d Inside = FVector3d(Params.Radius - 40.0, 0, 0);
	const FVector3d Above = FVector3d(Params.Radius + Params.MaxRelief + 20.0, 0, 0);
	TestTrue(TEXT("inside crust solid"), Stamp.SampleDensity(Inside) > 0.0f);
	TestTrue(TEXT("above peaks air"), Stamp.SampleDensity(Above) < 0.0f);

	float HMin = 1.0e9f;
	float HMax = -1.0e9f;
	int32 Ocean = 0;
	int32 Peak = 0;
	int32 Valley = 0;
	for (int32 I = 0; I < 512; ++I)
	{
		const float T = static_cast<float>(I) / 511.0f;
		const float Lon = T * 2.0f * PI * 3.7f;
		const float Lat = (T - 0.5f) * PI * 0.92f;
		const FVector3f Dir(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
		const FGXEarthField Field = Stamp.SampleEarthField(Dir, false);
		HMin = FMath::Min(HMin, Field.HeightM);
		HMax = FMath::Max(HMax, Field.HeightM);
		if (Field.HeightM < 0.0f)
		{
			++Ocean;
		}
		if (Field.HeightM > Params.MaxRelief * 0.28f)
		{
			++Peak;
		}
		if (Field.LandMask > 0.6f && Field.HeightM < Params.MaxRelief * 0.16f)
		{
			++Valley;
		}
	}

	// Spines are local. Random sphere samples can miss them — hit the
	// authored crests so "high peaks" is not a lottery.
	const FVector2f Crests[] = {
		FVector2f(8200.0f, 500.0f), FVector2f(10800.0f, 2600.0f),
		FVector2f(-1800.0f, 8800.0f), FVector2f(-8200.0f, -2600.0f),
		FVector2f(8200.0f, 2800.0f), FVector2f(-1800.0f, 11000.0f),
	};
	for (const FVector2f& C : Crests)
	{
		const FVector3f DirC = FVector3f(1.0f, C.X / Params.Radius, C.Y / Params.Radius).GetSafeNormal();
		const float CrestH = Stamp.SampleEarthField(DirC, false).HeightM;
		HMax = FMath::Max(HMax, CrestH);
		if (CrestH > Params.MaxRelief * 0.28f)
		{
			++Peak;
		}
	}

	TestTrue(TEXT("oceans exist"), Ocean > 8);
	TestTrue(TEXT("high peaks exist"), Peak > 4);
	TestTrue(TEXT("carved valleys exist"), Valley > 4);
	TestTrue(TEXT("relief spans hundreds of meters"), (HMax - HMin) > 600.0f);
	TestTrue(TEXT("highlands exist"), HMax > Params.MaxRelief * 0.20f);
	TestTrue(TEXT("east range is a real mountain"), HMax > 700.0f);

	{
		const FVector3f Volc = FVector3f(1.0f, 8100.0f / Params.Radius, 1100.0f / Params.Radius).GetSafeNormal();
		const FGXEarthField VF = Stamp.SampleEarthField(Volc, false);
		TestTrue(TEXT("east summit is a landmark"), VF.HeightM > 800.0f);
		TestTrue(TEXT("east summit registers"), VF.Volcano > 0.2f);
		const FVector3d VolcSurf(
			static_cast<double>(Volc.X) * (Params.Radius + VF.HeightM - 2.0f),
			static_cast<double>(Volc.Y) * (Params.Radius + VF.HeightM - 2.0f),
			static_cast<double>(Volc.Z) * (Params.Radius + VF.HeightM - 2.0f));
		const int32 VolcMat = Stamp.SampleMaterial(VolcSurf, 2.0f);
		TestTrue(TEXT("volcano is rock/snow/scorched, not grass"),
			VolcMat == static_cast<int32>(EGXVoxelMaterial::SnowIce)
			|| VolcMat == static_cast<int32>(EGXVoxelMaterial::VolcanicScorched)
			|| VolcMat == static_cast<int32>(EGXVoxelMaterial::RockyCliff));
		const FVector3d SpawnSurf(Params.Radius + 40.0, 0, 0);
		const int32 SpawnMat = Stamp.SampleMaterial(SpawnSurf, 2.0f);
		TestTrue(TEXT("spawn plains are grass, not sand"),
			SpawnMat == static_cast<int32>(EGXVoxelMaterial::TemperateGrass)
			|| SpawnMat == static_cast<int32>(EGXVoxelMaterial::DryDirt));
	}

	float F1 = 0.0f;
	float F2 = 0.0f;
	FGXNoise::WorleyF1F2(1.3f, -0.4f, 2.1f, 1337u, F1, F2);
	TestTrue(TEXT("Worley F1 <= F2"), F1 <= F2 + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Worley F1 finite"), F1 >= 0.0f && F1 < 4.0f);

	const uint64 FpA = Params.Fingerprint();
	FGXPlanetStampParams Other = Params;
	Other.MaxRelief = Params.MaxRelief + 50.0f;
	TestTrue(TEXT("fingerprint changes with relief"), FpA != Other.Fingerprint());
	TestTrue(TEXT("fingerprint stable"), FpA == Params.Fingerprint());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXVoxelSurfaceQueryCheap, "GX.Voxel.SurfaceQueryCheap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXVoxelSurfaceQueryCheap::RunTest(const FString& Parameters)
{
	const FGXPlanetStampParams Params = FGXPlanetStampParams::Earth();
	const FGXSphereStamp Stamp(Params);
	const FVector3f PlusX(1, 0, 0);
	const double T0 = FPlatformTime::Seconds();
	const float R = Stamp.SampleSurfaceRadius(PlusX);
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	TestTrue(TEXT("surface radius above mean"), R > Params.Radius);
	TestTrue(TEXT("one sample is cheap"), Ms < 15.0);
	return true;
}
