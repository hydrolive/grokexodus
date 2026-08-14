// Copyright Grok Exodus. All Rights Reserved.
// Port of FVoxelNoise — identical hash/fBm/ridged so identity tests pass.
#pragma once

#include "CoreMinimal.h"

struct FGXNoise
{
	static FORCEINLINE uint32 Hash(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u
			^ static_cast<uint32>(Y) * 668265263u
			^ static_cast<uint32>(Z) * 2147483647u
			^ Seed * 1013904223u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return H ^ (H >> 16);
	}

	static FORCEINLINE float HashToFloat(uint32 H)
	{
		return static_cast<float>(H & 0x00FFFFFF) / static_cast<float>(0x01000000);
	}

	static float ValueNoise3D(float X, float Y, float Z, uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 Z0 = FMath::FloorToInt(Z);
		const float Tx = X - static_cast<float>(X0);
		const float Ty = Y - static_cast<float>(Y0);
		const float Tz = Z - static_cast<float>(Z0);

		const float Sx = Tx * Tx * (3.0f - 2.0f * Tx);
		const float Sy = Ty * Ty * (3.0f - 2.0f * Ty);
		const float Sz = Tz * Tz * (3.0f - 2.0f * Tz);

		auto Corner = [&](int32 Xi, int32 Yi, int32 Zi) -> float
		{
			return HashToFloat(Hash(Xi, Yi, Zi, Seed)) * 2.0f - 1.0f;
		};

		const float N000 = Corner(X0, Y0, Z0);
		const float N100 = Corner(X0 + 1, Y0, Z0);
		const float N010 = Corner(X0, Y0 + 1, Z0);
		const float N110 = Corner(X0 + 1, Y0 + 1, Z0);
		const float N001 = Corner(X0, Y0, Z0 + 1);
		const float N101 = Corner(X0 + 1, Y0, Z0 + 1);
		const float N011 = Corner(X0, Y0 + 1, Z0 + 1);
		const float N111 = Corner(X0 + 1, Y0 + 1, Z0 + 1);

		const float NX00 = FMath::Lerp(N000, N100, Sx);
		const float NX10 = FMath::Lerp(N010, N110, Sx);
		const float NX01 = FMath::Lerp(N001, N101, Sx);
		const float NX11 = FMath::Lerp(N011, N111, Sx);
		const float NXY0 = FMath::Lerp(NX00, NX10, Sy);
		const float NXY1 = FMath::Lerp(NX01, NX11, Sy);
		return FMath::Lerp(NXY0, NXY1, Sz);
	}

	static float FBm(float X, float Y, float Z, uint32 Seed, int32 Octaves = 5, float Lacunarity = 2.0f, float Gain = 0.5f)
	{
		float Sum = 0.0f;
		float Amp = 1.0f;
		float Freq = 1.0f;
		float Norm = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += Amp * ValueNoise3D(X * Freq, Y * Freq, Z * Freq, Seed + static_cast<uint32>(I) * 17u);
			Norm += Amp;
			Amp *= Gain;
			Freq *= Lacunarity;
		}
		return (Norm > 0.0f) ? (Sum / Norm) : 0.0f;
	}

	static float Ridged(float X, float Y, float Z, uint32 Seed, int32 Octaves = 4)
	{
		float Sum = 0.0f;
		float Amp = 1.0f;
		float Freq = 1.0f;
		float Norm = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			const float N = ValueNoise3D(X * Freq, Y * Freq, Z * Freq, Seed + 91u + static_cast<uint32>(I));
			const float R = 1.0f - FMath::Abs(N);
			Sum += Amp * R * R;
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.0f;
		}
		return (Norm > 0.0f) ? (Sum / Norm) : 0.0f;
	}

	static FORCEINLINE float Smooth01(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** First and second nearest feature distances in a 3-D Worley cell. */
	static void WorleyF1F2(float X, float Y, float Z, uint32 Seed, float& OutF1, float& OutF2)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 Z0 = FMath::FloorToInt(Z);
		OutF1 = 8.0f;
		OutF2 = 8.0f;
		for (int32 Dz = -1; Dz <= 1; ++Dz)
		{
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					const int32 Cx = X0 + Dx;
					const int32 Cy = Y0 + Dy;
					const int32 Cz = Z0 + Dz;
					const uint32 H1 = Hash(Cx, Cy, Cz, Seed);
					const uint32 H2 = Hash(Cx, Cy, Cz, Seed ^ 0x9E3779B9u);
					const uint32 H3 = Hash(Cx, Cy, Cz, Seed ^ 0x85EBCA6Bu);
					const float Fx = static_cast<float>(Cx) + HashToFloat(H1);
					const float Fy = static_cast<float>(Cy) + HashToFloat(H2);
					const float Fz = static_cast<float>(Cz) + HashToFloat(H3);
					const float Ddx = Fx - X;
					const float Ddy = Fy - Y;
					const float Ddz = Fz - Z;
					const float Dist = FMath::Sqrt(Ddx * Ddx + Ddy * Ddy + Ddz * Ddz);
					if (Dist < OutF1)
					{
						OutF2 = OutF1;
						OutF1 = Dist;
					}
					else if (Dist < OutF2)
					{
						OutF2 = Dist;
					}
				}
			}
		}
	}

	static float WorleyF1(float X, float Y, float Z, uint32 Seed)
	{
		float F1 = 0.0f;
		float F2 = 0.0f;
		WorleyF1F2(X, Y, Z, Seed, F1, F2);
		return F1;
	}

	/** Cellular edge strength: 0 deep inside a cell, 1 on the shared face. */
	static float WorleyEdge(float X, float Y, float Z, uint32 Seed)
	{
		float F1 = 0.0f;
		float F2 = 0.0f;
		WorleyF1F2(X, Y, Z, Seed, F1, F2);
		return FMath::Clamp(F2 - F1, 0.0f, 1.0f);
	}
};
