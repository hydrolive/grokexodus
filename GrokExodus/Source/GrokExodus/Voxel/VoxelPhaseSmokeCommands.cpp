// Copyright Epic Games, Inc. All Rights Reserved.
// Phase 1–6 console smoke helpers + extended automation.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Voxel/VoxelSmokeTest.h"
#include "Voxel/VoxelMesher.h"
#include "Voxel/VoxelVolume.h"
#include "Voxel/VoxelPersistence.h"
#include "Voxel/VoxelRuntimeTextures.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

// Phase 1: mesh a surface chunk and report timings (pure data + mesh, no world)
static FAutoConsoleCommand GVoxelPhase1Cmd(
	TEXT("Voxel.Phase1Smoke"),
	TEXT("Mesh surface chunks on a small planet and log mesh time/memory."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FVoxelPlanetParams Params;
		Params.Radius = 128.0f;
		Params.MaxRelief = 16.0f;
		Params.VoxelSize = 1.0f;
		Params.Seed = 99u;

		FVoxelVolume Volume(Params);
		const FVector Surface(Params.Radius, 0, 0);
		const FIntVector VC = Volume.GetMapping().WorldToVoxel(Surface);
		const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VC);

		// Allocate a 3x3x3 neighborhood for apron
		for (int32 Z = -1; Z <= 1; ++Z)
		for (int32 Y = -1; Y <= 1; ++Y)
		for (int32 X = -1; X <= 1; ++X)
		{
			Volume.GetOrCreateChunk(FVoxelChunkCoord(CC.X + X, CC.Y + Y, CC.Z + Z));
		}

		const double T0 = FPlatformTime::Seconds();
		const FVoxelMeshData Mesh = FVoxelMesher::MeshChunk(Volume, CC);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;

		UE_LOG(LogVoxelWorld, Display, TEXT("Phase1Smoke: chunk(%d,%d,%d) verts=%d tris=%d meshMs=%.2f memBytes=%lld"),
			CC.X, CC.Y, CC.Z,
			Mesh.Positions.Num(),
			Mesh.Indices.Num() / 3,
			Ms,
			Volume.GetAllocatedMemoryBytes());

		const bool bOk = !Mesh.IsEmpty() && Mesh.Indices.Num() > 0;
		UE_LOG(LogVoxelWorld, Display, TEXT("Voxel.Phase1Smoke: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
	}));

// Phase 2: dig tunnel + serialize identity
static FAutoConsoleCommand GVoxelPhase2Cmd(
	TEXT("Voxel.Phase2Smoke"),
	TEXT("Dig a tunnel, place seal, save/load, verify deformation."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FVoxelPlanetParams Params;
		Params.Radius = 96.0f;
		Params.MaxRelief = 10.0f;
		Params.Seed = 5u;
		FVoxelVolume Volume(Params);

		const FVector Start(Params.Radius - 1.0f, 0, 0);
		FVoxelToolModifiers Tool;
		Tool.DigSpeedMul = 8.0f;

		// Tunnel along -X into planet
		for (int32 I = 0; I < 12; ++I)
		{
			const FVector C = Start - FVector(static_cast<float>(I) * 1.5f, 0, 0);
			Volume.ApplySphereBrush(C, 1.8f, true, 0, Tool, 1.0f);
		}
		// Overhang cave
		Volume.ApplySphereBrush(Start - FVector(8, 0, 2), 2.5f, true, 0, Tool, 1.0f);
		// Place ramp seal
		Volume.ApplySphereBrush(Start - FVector(2, 0, 0), 1.2f, false, static_cast<int32>(EVoxelMaterialId::RockyCliff), Tool, 1.0f);

		const FVector Probe = Start - FVector(6, 0, 0);
		const FVoxelCell Before = Volume.SampleCell(Probe);

		TArray<uint8> Buf;
		FVoxelPersistence::SaveToBuffer(Volume, Buf);
		FVoxelVolume Loaded(Params);
		FVoxelPersistence::LoadFromBuffer(Loaded, Buf);
		const FVoxelCell After = Loaded.SampleCell(Probe);

		const bool bOk = Before.MaterialId == After.MaterialId
			&& FMath::IsNearlyEqual(Before.Density, After.Density, 0.01f);
		UE_LOG(LogVoxelWorld, Display, TEXT("Phase2Smoke: tunnel dens=%.2f mat=%d saveBytes=%d → %s"),
			Before.Density, Before.MaterialId, Buf.Num(), bOk ? TEXT("PASS") : TEXT("FAIL"));
	}));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelPhase1MeshAutomationTest,
	"GrokExodus.Voxel.Phase1.Mesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelPhase1MeshAutomationTest::RunTest(const FString& Parameters)
{
	FVoxelPlanetParams Params;
	Params.Radius = 64.0f;
	Params.MaxRelief = 8.0f;
	Params.Seed = 1u;
	FVoxelVolume Volume(Params);
	const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(
		Volume.GetMapping().WorldToVoxel(FVector(Params.Radius, 0, 0)));
	for (int32 Z = -1; Z <= 1; ++Z)
	for (int32 Y = -1; Y <= 1; ++Y)
	for (int32 X = -1; X <= 1; ++X)
	{
		Volume.GetOrCreateChunk(FVoxelChunkCoord(CC.X + X, CC.Y + Y, CC.Z + Z));
	}
	const FVoxelMeshData Mesh = FVoxelMesher::MeshChunk(Volume, CC);
	TestTrue(TEXT("Mesh has triangles"), Mesh.Indices.Num() >= 3);
	TestTrue(TEXT("Mesh has verts"), Mesh.Positions.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelPhase2PersistAutomationTest,
	"GrokExodus.Voxel.Phase2.PersistEdit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelPhase2PersistAutomationTest::RunTest(const FString& Parameters)
{
	FVoxelPlanetParams Params;
	Params.Radius = 48.0f;
	Params.MaxRelief = 6.0f;
	Params.Seed = 3u;
	FVoxelVolume Volume(Params);
	FVoxelToolModifiers Tool;
	Tool.DigSpeedMul = 10.0f;
	const FVector C(Params.Radius - 2.f, 0, 0);
	Volume.ApplySphereBrush(C, 3.f, true, 0, Tool, 1.f);
	Volume.ApplySphereBrush(C, 1.5f, false, 2, Tool, 1.f);
	TArray<uint8> A, B;
	FVoxelPersistence::SaveToBuffer(Volume, A);
	FVoxelVolume V2(Params);
	FVoxelPersistence::LoadFromBuffer(V2, A);
	FVoxelPersistence::SaveToBuffer(V2, B);
	TestTrue(TEXT("Edit persist round-trip"), FVoxelPersistence::BuffersEqual(A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelPhase3TextureAutomationTest,
	"GrokExodus.Voxel.Phase3.Textures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelPhase3TextureAutomationTest::RunTest(const FString& Parameters)
{
	const FString Dir = FVoxelRuntimeTextures::GetSourceTextureDir();
	const bool bGrass = FPaths::FileExists(Dir / TEXT("T_TemperateGrass_A.jpg"));
	const bool bRock = FPaths::FileExists(Dir / TEXT("T_RockyCliff_A.jpg"));
	TestTrue(TEXT("Grass albedo source exists"), bGrass);
	TestTrue(TEXT("Rock albedo source exists"), bRock);

	// LOD mesh produces fewer verts than LOD0 on same chunk
	FVoxelPlanetParams Params;
	Params.Radius = 64.0f;
	Params.MaxRelief = 8.0f;
	Params.Seed = 1u;
	FVoxelVolume Volume(Params);
	const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(
		Volume.GetMapping().WorldToVoxel(FVector(Params.Radius, 0, 0)));
	for (int32 Z = -1; Z <= 1; ++Z)
	for (int32 Y = -1; Y <= 1; ++Y)
	for (int32 X = -1; X <= 1; ++X)
	{
		Volume.GetOrCreateChunk(FVoxelChunkCoord(CC.X + X, CC.Y + Y, CC.Z + Z));
	}

	FVoxelMesher::FSettings S0;
	S0.LOD = 0;
	FVoxelMesher::FSettings S2;
	S2.LOD = 2;
	const FVoxelMeshData M0 = FVoxelMesher::MeshChunk(Volume, CC, S0);
	const FVoxelMeshData M2 = FVoxelMesher::MeshChunk(Volume, CC, S2);
	TestTrue(TEXT("LOD0 has mesh"), M0.Positions.Num() > 0);
	TestTrue(TEXT("LOD2 has fewer verts than LOD0"), M2.Positions.Num() < M0.Positions.Num() || M2.IsEmpty());
	TestTrue(TEXT("MaterialIds parallel verts"), M0.MaterialIds.Num() == M0.Positions.Num());
	return true;
}

// Console: force reload textures + report
static FAutoConsoleCommand GVoxelPhase3Cmd(
	TEXT("Voxel.Phase3Smoke"),
	TEXT("Verify texture sources and LOD mesh reduction."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const FString Dir = FVoxelRuntimeTextures::GetSourceTextureDir();
		int32 Found = 0;
		const TCHAR* Names[] = {
			TEXT("T_TemperateGrass_A.jpg"), TEXT("T_RockyCliff_A.jpg"), TEXT("T_DryDirt_A.jpg"),
			TEXT("T_SandCoastal_A.jpg"), TEXT("T_SnowIce_A.jpg"), TEXT("T_WetMud_A.jpg"),
			TEXT("T_VolcanicScorched_A.jpg")
		};
		for (const TCHAR* N : Names)
		{
			if (FPaths::FileExists(Dir / N)) ++Found;
		}
		UE_LOG(LogVoxelWorld, Display, TEXT("Phase3Smoke: %d/7 albedo sources in %s"), Found, *Dir);

		FVoxelPlanetParams Params;
		Params.Radius = 80.0f;
		Params.MaxRelief = 10.0f;
		FVoxelVolume Volume(Params);
		const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(
			Volume.GetMapping().WorldToVoxel(FVector(Params.Radius, 0, 0)));
		for (int32 Z = -1; Z <= 1; ++Z)
		for (int32 Y = -1; Y <= 1; ++Y)
		for (int32 X = -1; X <= 1; ++X)
			Volume.GetOrCreateChunk(FVoxelChunkCoord(CC.X + X, CC.Y + Y, CC.Z + Z));

		FVoxelMesher::FSettings S0; S0.LOD = 0;
		FVoxelMesher::FSettings S2; S2.LOD = 2;
		const FVoxelMeshData M0 = FVoxelMesher::MeshChunk(Volume, CC, S0);
		const FVoxelMeshData M2 = FVoxelMesher::MeshChunk(Volume, CC, S2);
		UE_LOG(LogVoxelWorld, Display, TEXT("Phase3Smoke LOD0 verts=%d LOD2 verts=%d → %s"),
			M0.Positions.Num(), M2.Positions.Num(),
			(Found >= 7 && M0.Positions.Num() > M2.Positions.Num()) ? TEXT("PASS") : TEXT("CHECK"));
	}));
