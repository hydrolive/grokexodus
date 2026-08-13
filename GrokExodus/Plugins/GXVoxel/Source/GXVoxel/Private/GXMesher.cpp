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
	Densities.SetNumUninitialized(SampleCount);
	Materials.SetNumUninitialized(SampleCount);

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
				const FGXVoxelPacked Packed = Snapshot.Sample(Corner);
				const int32 Idx = GridIndex(X, Y, Z, Samples, Samples);
				// Prefer full-precision stamp density so the ±32 m packed clamp
				// cannot flatten an entire unedited crust cell to one sign.
				const bool bEdited = (Packed.Flags & (EGXVoxelFlags::Deformed | EGXVoxelFlags::PlayerPlaced)) != 0;
				Densities[Idx] = bEdited ? Packed.ToDensityMeters() : Stamp.SampleDensity(Corner);
				Materials[Idx] = Packed.Material != 0 ? Packed.Material : (Densities[Idx] > 0.0f ? 1 : 0);
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
		const int32 MatId = SolidMat != 0 ? SolidMat : ((Ma != 0) ? Ma : (Mb != 0 ? Mb : 1));

		const int32 VI = Mesh.Positions.Num();
		Mesh.Positions.Add(World);
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.UV0.Add(FVector2D(float(MatId), float(Settings.LOD)));
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
					FVector FN = FVector::CrossProduct(Mesh.Positions[I1] - Mesh.Positions[I0], Mesh.Positions[I2] - Mesh.Positions[I0]);
					const FVector Centroid = (Mesh.Positions[I0] + Mesh.Positions[I1] + Mesh.Positions[I2]) / 3.f;
					if (FVector::DotProduct(FN, Centroid) < 0.f)
					{
						Mesh.Indices.Add(I0); Mesh.Indices.Add(I2); Mesh.Indices.Add(I1);
					}
					else
					{
						Mesh.Indices.Add(I0); Mesh.Indices.Add(I1); Mesh.Indices.Add(I2);
					}
				}
			}
		}
	}

	if (Settings.bComputeNormals && Mesh.Positions.Num() > 0)
	{
		for (FVector& N : Mesh.Normals)
		{
			N = FVector::ZeroVector;
		}
		for (int32 I = 0; I + 2 < Mesh.Indices.Num(); I += 3)
		{
			const FVector FN = FVector::CrossProduct(
				Mesh.Positions[Mesh.Indices[I + 1]] - Mesh.Positions[Mesh.Indices[I]],
				Mesh.Positions[Mesh.Indices[I + 2]] - Mesh.Positions[Mesh.Indices[I]]);
			Mesh.Normals[Mesh.Indices[I]] += FN;
			Mesh.Normals[Mesh.Indices[I + 1]] += FN;
			Mesh.Normals[Mesh.Indices[I + 2]] += FN;
		}
		for (int32 I = 0; I < Mesh.Normals.Num(); ++I)
		{
			if (!Mesh.Normals[I].Normalize())
			{
				const FVector R = Mesh.Positions[I].GetSafeNormal();
				Mesh.Normals[I] = R.IsNearlyZero() ? FVector::UpVector : R;
			}
			if (FVector::DotProduct(Mesh.Normals[I], Mesh.Positions[I]) < 0.f)
			{
				Mesh.Normals[I] = -Mesh.Normals[I];
			}
		}
	}

	(void)Stamp;
	return Mesh;
}
