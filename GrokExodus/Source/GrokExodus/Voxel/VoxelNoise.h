// Copyright Epic Games, Inc. All Rights Reserved.
// Lightweight hash noise for procedural planet density (no external deps).

#pragma once

#include "CoreMinimal.h"

/**
 * Deterministic value / fBm noise for spherical continents & mountains.
 * Seedable; pure functions only.
 */
struct FVoxelNoise
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
		// [0,1)
		return static_cast<float>(H & 0x00FFFFFF) / static_cast<float>(0x01000000);
	}

	/** Value noise in [-1, 1] at integer lattice. */
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

	/** Fractal Brownian motion. Amplitude sum approaches ~2 for default gain. */
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

	/** Ridged noise for mountain spines. */
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
};
