// Copyright Grok Exodus. All Rights Reserved.

#include "GXMesher.h"
#include "GXVoxelStamps.h"
#include "GXVoxelVolume.h"

#include "GXMarchingCubesTables.inl"

FGXMeshBuffers FGXMesher::MeshChunk(
	const FGXVoxelSnapshot& Snapshot,
	const FGXChunkKey& Coord,
	const FSettings& Settings)
{
	using namespace GXMC;

	FGXMeshBuffers Mesh;
	const int32 LOD = FMath::Clamp(Settings.LOD, 0, 5);
	const int32 Stride = 1 << LOD;
	const float BaseVoxel = Snapshot.Params.VoxelSize;
	const float VoxelSize = BaseVoxel * static_cast<float>(Stride);
	constexpr int32 CS = FGXVoxelConstants::ChunkSize;
	const int32 Cells = FMath::Max(2, CS / Stride);
	constexpr int32 Pad = 1;
	const int32 Samples = Cells + 1 + 2 * Pad;
	const int32 SampleCount = Samples * Samples * Samples;

	TArray<float> Densities;
	TArray<int32> Materials;
	TArray<uint8> Auth;
	Densities.SetNumUninitialized(SampleCount);
	Materials.SetNumUninitialized(SampleCount);
	Auth.SetNumZeroed(SampleCount);

	const int32 BaseMinX = Coord.X * CS - Pad * Stride;
	const int32 BaseMinY = Coord.Y * CS - Pad * Stride;
	const int32 BaseMinZ = Coord.Z * CS - Pad * Stride;

	FGXSphereStamp Stamp(Snapshot.Params);

	for (int32 Z = 0; Z < Samples; ++Z)
	{
		for (int32 Y = 0; Y < Samples; ++Y)
		{
			for (int32 X = 0; X < Samples; ++X)
			{
				const FVector3d Corner(
					static_cast<double>(BaseMinX + X * Stride) * BaseVoxel,
					static_cast<double>(BaseMinY + Y * Stride) * BaseVoxel,
					static_cast<double>(BaseMinZ + Z * Stride) * BaseVoxel);
				const int32 Idx = GridIndex(X, Y, Z, Samples, Samples);
				FGXVoxelPacked Packed;
				if (Snapshot.TryGetAuthoritative(Corner, Packed))
				{
					Densities[Idx] = Packed.ToDensityMeters();
					Materials[Idx] = Packed.Material;
					Auth[Idx] = 1;
				}
				else
				{
					// Live stamp only. A stale crust atlas sat the MC lid
					// metres off the walk tiles so consume opened a window
					// through the planet (0.13.25 hole).
					Densities[Idx] = Stamp.SampleDensity(Corner);
					Materials[Idx] = Densities[Idx] > 0.0f ? 1 : 0;
				}
			}
		}
	}

	const FVector Origin(
		static_cast<float>(BaseMinX) * BaseVoxel,
		static_cast<float>(BaseMinY) * BaseVoxel,
		static_cast<float>(BaseMinZ) * BaseVoxel);

	const int32 SizeX = Samples, SizeY = Samples, SizeZ = Samples;
	const int32 SafePad = Pad;
	const int32 X0 = SafePad, Y0 = SafePad, Z0 = SafePad;
	const int32 X1 = SizeX - 1 - SafePad;
	const int32 Y1 = SizeY - 1 - SafePad;
	const int32 Z1 = SizeZ - 1 - SafePad;
	if (X1 <= X0 || Y1 <= Y0 || Z1 <= Z0)
	{
		return Mesh;
	}

	auto SampleD = [&](int32 X, int32 Y, int32 Z) -> float
	{
		return Densities[GridIndex(X, Y, Z, SizeX, SizeY)];
	};
	auto SampleM = [&](int32 X, int32 Y, int32 Z) -> int32
	{
		return Materials[GridIndex(X, Y, Z, SizeX, SizeY)];
	};
	auto SampleA = [&](int32 X, int32 Y, int32 Z) -> uint8
	{
		return Auth[GridIndex(X, Y, Z, SizeX, SizeY)];
	};

	TMap<uint64, int32> EdgeVert;
	auto EdgeKey = [](int32 X, int32 Y, int32 Z, int32 Axis) -> uint64
	{
		return (uint64(uint32(X) & 0xFFFF) << 48)
			| (uint64(uint32(Y) & 0xFFFF) << 32)
			| (uint64(uint32(Z) & 0xFFFF) << 16)
			| uint64(Axis & 0xFF);
	};

	auto GetOrCreateEdgeVert = [&](int32 C0x, int32 C0y, int32 C0z, int32 C1x, int32 C1y, int32 C1z,
		float D0, float D1, int32 M0, int32 M1) -> int32
	{
		int32 Ax = C0x, Ay = C0y, Az = C0z, Bx = C1x, By = C1y, Bz = C1z;
		float Da = D0, Db = D1;
		int32 Ma = M0, Mb = M1;
		if (Ax > Bx || (Ax == Bx && Ay > By) || (Ax == Bx && Ay == By && Az > Bz))
		{
			Swap(Ax, Bx); Swap(Ay, By); Swap(Az, Bz);
			Swap(Da, Db); Swap(Ma, Mb);
		}
		const int32 Axis = (Bx != Ax) ? 0 : ((By != Ay) ? 1 : 2);
		const uint64 Key = EdgeKey(Ax, Ay, Az, Axis);
		if (const int32* Found = EdgeVert.Find(Key))
		{
			return *Found;
		}

		const float T = FMath::Clamp((Settings.IsoLevel - Da) / (Db - Da + KINDA_SMALL_NUMBER), 0.f, 1.f);
		const FVector Local = FMath::Lerp(FVector(float(Ax), float(Ay), float(Az)), FVector(float(Bx), float(By), float(Bz)), T);
		const FVector World = Origin + Local * VoxelSize;
		const int32 SolidMat = (Da >= Settings.IsoLevel) ? Ma : Mb;
		int32 MatId = SolidMat != 0 ? SolidMat : ((Ma != 0) ? Ma : (Mb != 0 ? Mb : 1));
		// Edited air/solid grass faces are scraped soil, not a lawn.
		if (MatId == 1 && (SampleA(Ax, Ay, Az) || SampleA(Bx, By, Bz)))
		{
			MatId = 3;
		}
		int32 AtlasId = MatId;
		if (AtlasId <= 0) AtlasId = 1;
		if (AtlasId == 8 || AtlasId == 9 || AtlasId == 12) AtlasId = 2;
		if (AtlasId == 10) AtlasId = 3;
		if (AtlasId == 11) AtlasId = 5;
		AtlasId = FMath::Clamp(AtlasId, 0, 7);

		const int32 VI = Mesh.Positions.Num();
		Mesh.Positions.Add(World);
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.UV0.Add(FVector2D(static_cast<float>(AtlasId), static_cast<float>(Settings.LOD)));
		FLinearColor Col = GXMaterialDebugColor(MatId);
		Col.R = FMath::Clamp(Col.R * 1.3f + 0.08f, 0.f, 1.f);
		Col.G = FMath::Clamp(Col.G * 1.3f + 0.08f, 0.f, 1.f);
		Col.B = FMath::Clamp(Col.B * 1.3f + 0.08f, 0.f, 1.f);
		Col.A = 1.f;
		Mesh.Colors.Add(Col);
		Mesh.MaterialIds.Add(MatId);
		EdgeVert.Add(Key, VI);
		return VI;
	};

	const int32 COff[8][3] = {
		{0,0,0},{1,0,0},{1,1,0},{0,1,0},
		{0,0,1},{1,0,1},{1,1,1},{0,1,1}
	};

	for (int32 Z = Z0; Z < Z1; ++Z)
	{
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				float Cd[8];
				int32 Cm[8];
				int32 CubeIndex = 0;
				for (int32 C = 0; C < 8; ++C)
				{
					Cd[C] = SampleD(X + COff[C][0], Y + COff[C][1], Z + COff[C][2]);
					Cm[C] = SampleM(X + COff[C][0], Y + COff[C][1], Z + COff[C][2]);
					if (Cd[C] >= Settings.IsoLevel)
					{
						CubeIndex |= (1 << C);
					}
				}
				const int32 Edges = EdgeTable[CubeIndex];
				if (Edges == 0)
				{
					continue;
				}
				int32 VertList[12];
				for (int32 E = 0; E < 12; ++E)
				{
					VertList[E] = -1;
					if (Edges & (1 << E))
					{
						const int32 A = EdgeCorner[E][0];
						const int32 B = EdgeCorner[E][1];
						VertList[E] = GetOrCreateEdgeVert(
							X + COff[A][0], Y + COff[A][1], Z + COff[A][2],
							X + COff[B][0], Y + COff[B][1], Z + COff[B][2],
							Cd[A], Cd[B], Cm[A], Cm[B]);
					}
				}
				for (int32 I = 0; TriTable[CubeIndex][I] != -1; I += 3)
				{
					const int32 I0 = VertList[TriTable[CubeIndex][I]];
					const int32 I1 = VertList[TriTable[CubeIndex][I + 1]];
					const int32 I2 = VertList[TriTable[CubeIndex][I + 2]];
					if (I0 < 0 || I1 < 0 || I2 < 0)
					{
						continue;
					}
					// Standard table order. I0,I2,I1 pointed the crust inward — PBR is
					// single-sided so the surface vanished and only the underside showed.
					Mesh.Indices.Add(I0);
					Mesh.Indices.Add(I1);
					Mesh.Indices.Add(I2);
				}
			}
		}
	}

	if (Settings.bComputeNormals && Mesh.Positions.Num() > 0)
	{
		// SDF gradient is continuous across chunk faces. Face-averaged normals
		// were a different vector on each side of the 32 m grid (dark seams).
		auto SampleClamp = [&](int32 X, int32 Y, int32 Z) -> float
		{
			X = FMath::Clamp(X, 0, SizeX - 1);
			Y = FMath::Clamp(Y, 0, SizeY - 1);
			Z = FMath::Clamp(Z, 0, SizeZ - 1);
			return Densities[GridIndex(X, Y, Z, SizeX, SizeY)];
		};
		for (int32 I = 0; I < Mesh.Positions.Num(); ++I)
		{
			const FVector G(
				(Mesh.Positions[I].X - Origin.X) / VoxelSize,
				(Mesh.Positions[I].Y - Origin.Y) / VoxelSize,
				(Mesh.Positions[I].Z - Origin.Z) / VoxelSize);
			const int32 IX = FMath::RoundToInt(G.X);
			const int32 IY = FMath::RoundToInt(G.Y);
			const int32 IZ = FMath::RoundToInt(G.Z);
			FVector Grad(
				SampleClamp(IX + 1, IY, IZ) - SampleClamp(IX - 1, IY, IZ),
				SampleClamp(IX, IY + 1, IZ) - SampleClamp(IX, IY - 1, IZ),
				SampleClamp(IX, IY, IZ + 1) - SampleClamp(IX, IY, IZ - 1));
			if (!Grad.Normalize())
			{
				Grad = Mesh.Positions[I].GetSafeNormal();
			}
			// Grad points into solid. Front faces air — cave walls must N
			// toward the hole, not planet-outward (that was orange/blue).
			Mesh.Normals[I] = -Grad;
		}
	}

	// LOD0 cave meshes do not need a LOD stitch. Skirts there became
	// floating black slabs and poles the player could not dig or fill.
	if (Settings.bTransvoxelSkirts && Settings.LOD >= 1 && Mesh.Indices.Num() >= 3)
	{
		const float ChunkM = BaseVoxel * static_cast<float>(CS);
		const FVector BoxMin(Coord.X * ChunkM, Coord.Y * ChunkM, Coord.Z * ChunkM);
		const FVector BoxMax = BoxMin + FVector(ChunkM);
		const float FaceEps = VoxelSize * 0.35f;
		const float DropM = FMath::Max(VoxelSize * 1.35f, BaseVoxel * 1.25f);
		auto OnFace = [&](const FVector& P) -> bool
		{
			return FMath::Abs(P.X - BoxMin.X) <= FaceEps || FMath::Abs(P.X - BoxMax.X) <= FaceEps
				|| FMath::Abs(P.Y - BoxMin.Y) <= FaceEps || FMath::Abs(P.Y - BoxMax.Y) <= FaceEps
				|| FMath::Abs(P.Z - BoxMin.Z) <= FaceEps || FMath::Abs(P.Z - BoxMax.Z) <= FaceEps;
		};

		TMap<uint64, int32> EdgeUse;
		TMap<uint64, FIntVector> EdgeAB;
		auto Pack = [](int32 A, int32 B) -> uint64
		{
			const int32 Lo = FMath::Min(A, B);
			const int32 Hi = FMath::Max(A, B);
			return (uint64(uint32(Lo)) << 32) | uint32(Hi);
		};
		for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
		{
			const int32 V[3] = { Mesh.Indices[T], Mesh.Indices[T + 1], Mesh.Indices[T + 2] };
			for (int32 E = 0; E < 3; ++E)
			{
				const int32 A = V[E];
				const int32 B = V[(E + 1) % 3];
				const uint64 Key = Pack(A, B);
				int32& Cnt = EdgeUse.FindOrAdd(Key);
				++Cnt;
				if (Cnt == 1)
				{
					EdgeAB.Add(Key, FIntVector(A, B, 0));
				}
			}
		}

		const int32 FirstSkirt = Mesh.Positions.Num();
		for (const auto& Pair : EdgeUse)
		{
			if (Pair.Value != 1)
			{
				continue;
			}
			const FIntVector AB = EdgeAB.FindRef(Pair.Key);
			const int32 A = AB.X;
			const int32 B = AB.Y;
			if (!Mesh.Positions.IsValidIndex(A) || !Mesh.Positions.IsValidIndex(B))
			{
				continue;
			}
			if (!OnFace(Mesh.Positions[A]) || !OnFace(Mesh.Positions[B]))
			{
				continue;
			}
			auto Drop = [&](int32 Src) -> int32
			{
				const FVector P = Mesh.Positions[Src];
				FVector Rad = P.GetSafeNormal();
				if (Rad.IsNearlyZero())
				{
					Rad = FVector(1, 0, 0);
				}
				const FVector N = Mesh.Normals.IsValidIndex(Src) ? Mesh.Normals[Src] : -Rad;
				const FVector2D UV = Mesh.UV0.IsValidIndex(Src) ? Mesh.UV0[Src] : FVector2D(1.0f, 0.0f);
				const FLinearColor Col = Mesh.Colors.IsValidIndex(Src) ? Mesh.Colors[Src] : FLinearColor::White;
				const int32 Mat = Mesh.MaterialIds.IsValidIndex(Src) ? Mesh.MaterialIds[Src] : 1;
				const int32 Idx = Mesh.Positions.Num();
				Mesh.Positions.Add(P - Rad * DropM);
				Mesh.Normals.Add(N);
				Mesh.UV0.Add(UV);
				Mesh.Colors.Add(Col);
				Mesh.MaterialIds.Add(Mat);
				return Idx;
			};
			const int32 A2 = Drop(A);
			const int32 B2 = Drop(B);
			// Hang the flap toward the core. Winding matches I0,I1,I2 air-facing.
			Mesh.Indices.Add(A); Mesh.Indices.Add(B); Mesh.Indices.Add(B2);
			Mesh.Indices.Add(A); Mesh.Indices.Add(B2); Mesh.Indices.Add(A2);
		}
		(void)FirstSkirt;
	}

	(void)Stamp;
	return Mesh;
}
