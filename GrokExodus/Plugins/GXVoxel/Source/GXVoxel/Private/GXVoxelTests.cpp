// Copyright Grok Exodus. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GXVoxelVolume.h"
#include "GXMesher.h"
#include "GXNoise.h"

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
