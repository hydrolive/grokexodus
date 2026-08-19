// Copyright Grok Exodus. All Rights Reserved.

#include "GXEditIsland.h"

void FGXEditIsland::Reset()
{
	Spheres.Reset();
	PatchFace = -1;
	PatchU0 = PatchU1 = PatchV0 = PatchV1 = 0.0f;
}

int8 FGXEditIsland::FaceOf(const FVector& Dir)
{
	const FVector A(FMath::Abs(Dir.X), FMath::Abs(Dir.Y), FMath::Abs(Dir.Z));
	if (A.X >= A.Y && A.X >= A.Z)
	{
		return Dir.X >= 0.0f ? 0 : 1;
	}
	if (A.Y >= A.Z)
	{
		return Dir.Y >= 0.0f ? 2 : 3;
	}
	return Dir.Z >= 0.0f ? 4 : 5;
}

void FGXEditIsland::FaceAxes(int8 Face, FVector& OutN, FVector& OutT, FVector& OutB)
{
	switch (Face)
	{
	case 0: OutN = FVector(1, 0, 0); OutT = FVector(0, 1, 0); OutB = FVector(0, 0, 1); break;
	case 1: OutN = FVector(-1, 0, 0); OutT = FVector(0, -1, 0); OutB = FVector(0, 0, 1); break;
	case 2: OutN = FVector(0, 1, 0); OutT = FVector(-1, 0, 0); OutB = FVector(0, 0, 1); break;
	case 3: OutN = FVector(0, -1, 0); OutT = FVector(1, 0, 0); OutB = FVector(0, 0, 1); break;
	case 4: OutN = FVector(0, 0, 1); OutT = FVector(0, 1, 0); OutB = FVector(-1, 0, 0); break;
	default: OutN = FVector(0, 0, -1); OutT = FVector(0, 1, 0); OutB = FVector(1, 0, 0); break;
	}
	// Must match FGXCrustTiles::FaceAxes — walk grid is rotated ~29°.
	constexpr float Ca = 0.8750f;
	constexpr float Sa = 0.4848f;
	const FVector Tr = (OutT * Ca + OutB * Sa).GetSafeNormal();
	const FVector Br = (OutB * Ca - OutT * Sa).GetSafeNormal();
	OutT = Tr;
	OutB = Br;
}

void FGXEditIsland::ProjectUV(const FVector& P, float& OutU, float& OutV) const
{
	FVector N, T, B;
	FaceAxes(HasPatch() ? PatchFace : FaceOf(P), N, T, B);
	OutU = static_cast<float>(FVector::DotProduct(P, T));
	OutV = static_cast<float>(FVector::DotProduct(P, B));
}

bool FGXEditIsland::Contains(const FVector& P) const
{
	return ContainsPadded(P, 0.0f);
}

bool FGXEditIsland::ContainsPadded(const FVector& P, float PadM) const
{
	if (HasPatch())
	{
		float U = 0.0f, V = 0.0f;
		ProjectUV(P, U, V);
		return U >= PatchU0 - PadM && U <= PatchU1 + PadM
			&& V >= PatchV0 - PadM && V <= PatchV1 + PadM;
	}
	for (const FGXEditSphere& S : Spheres)
	{
		if (S.R > 0.0f && FVector::DistSquared(P, S.C) <= FMath::Square(S.R + PadM))
		{
			return true;
		}
	}
	return false;
}

bool FGXEditIsland::OverlapsBox(const FBox& Box) const
{
	if (!Box.IsValid)
	{
		return false;
	}
	if (ContainsPadded(Box.GetCenter(), CellM))
	{
		return true;
	}
	const FVector C[8] = {
		FVector(Box.Min.X, Box.Min.Y, Box.Min.Z),
		FVector(Box.Max.X, Box.Min.Y, Box.Min.Z),
		FVector(Box.Min.X, Box.Max.Y, Box.Min.Z),
		FVector(Box.Max.X, Box.Max.Y, Box.Min.Z),
		FVector(Box.Min.X, Box.Min.Y, Box.Max.Z),
		FVector(Box.Max.X, Box.Min.Y, Box.Max.Z),
		FVector(Box.Min.X, Box.Max.Y, Box.Max.Z),
		FVector(Box.Max.X, Box.Max.Y, Box.Max.Z),
	};
	for (int32 I = 0; I < 8; ++I)
	{
		if (ContainsPadded(C[I], CellM))
		{
			return true;
		}
	}
	if (HasPatch())
	{
		float U0 = 1.0e12f, U1 = -1.0e12f, V0 = 1.0e12f, V1 = -1.0e12f;
		for (int32 I = 0; I < 8; ++I)
		{
			float U = 0.0f, V = 0.0f;
			ProjectUV(C[I], U, V);
			U0 = FMath::Min(U0, U);
			U1 = FMath::Max(U1, U);
			V0 = FMath::Min(V0, V);
			V1 = FMath::Max(V1, V);
		}
		return !(U1 < PatchU0 || U0 > PatchU1 || V1 < PatchV0 || V0 > PatchV1);
	}
	return false;
}

void FGXEditIsland::GrowPatch(const FVector& Center, float RadiusM)
{
	const int8 Face = HasPatch() ? PatchFace : FaceOf(Center);
	FVector N, T, B;
	FaceAxes(Face, N, T, B);
	const float U = static_cast<float>(FVector::DotProduct(Center, T));
	const float V = static_cast<float>(FVector::DotProduct(Center, B));
	const float Reach = RadiusM + MarginM;
	const float Half = HasPatch() ? Reach : FMath::Max(MinHalfM, Reach);
	auto Down = [](float X) { return FMath::FloorToFloat(X / CellM) * CellM; };
	auto Up = [](float X) { return FMath::CeilToFloat(X / CellM) * CellM; };
	if (!HasPatch())
	{
		PatchFace = Face;
		PatchU0 = Down(U - Half);
		PatchU1 = Up(U + Half);
		PatchV0 = Down(V - Half);
		PatchV1 = Up(V + Half);
		return;
	}
	PatchU0 = FMath::Min(PatchU0, Down(U - Reach));
	PatchU1 = FMath::Max(PatchU1, Up(U + Reach));
	PatchV0 = FMath::Min(PatchV0, Down(V - Reach));
	PatchV1 = FMath::Max(PatchV1, Up(V + Reach));
	const float Wu = PatchU1 - PatchU0;
	const float Wv = PatchV1 - PatchV0;
	if (Wu > MaxExtentM)
	{
		const float Mid = 0.5f * (PatchU0 + PatchU1);
		PatchU0 = Mid - MaxExtentM * 0.5f;
		PatchU1 = Mid + MaxExtentM * 0.5f;
	}
	if (Wv > MaxExtentM)
	{
		const float Mid = 0.5f * (PatchV0 + PatchV1);
		PatchV0 = Mid - MaxExtentM * 0.5f;
		PatchV1 = Mid + MaxExtentM * 0.5f;
	}
}

void FGXEditIsland::Add(const FVector& Center, float RadiusM)
{
	if (RadiusM <= 0.0f)
	{
		return;
	}
	GrowPatch(Center, RadiusM);
	for (FGXEditSphere& S : Spheres)
	{
		const float D = FVector::Dist(Center, S.C);
		if (D + RadiusM <= S.R + 0.05f)
		{
			return;
		}
		if (D + S.R <= RadiusM + 0.05f)
		{
			S.C = Center;
			S.R = RadiusM;
			return;
		}
	}
	if (Spheres.Num() >= MaxSpheres)
	{
		return;
	}
	FGXEditSphere& N = Spheres.AddDefaulted_GetRef();
	N.C = Center;
	N.R = RadiusM;
}

FBox FGXEditIsland::Bounds() const
{
	FBox Box(ForceInit);
	if (HasPatch())
	{
		FVector N, T, B;
		FaceAxes(PatchFace, N, T, B);
		float Mag = 60000.0f;
		if (Spheres.Num() > 0)
		{
			Mag = static_cast<float>(Spheres[0].C.Size());
		}
		const float Us[2] = { PatchU0, PatchU1 };
		const float Vs[2] = { PatchV0, PatchV1 };
		for (int32 IU = 0; IU < 2; ++IU)
		{
			for (int32 IV = 0; IV < 2; ++IV)
			{
				Box += N * Mag + T * Us[IU] + B * Vs[IV];
			}
		}
		Box = Box.ExpandBy(20.0f);
	}
	for (const FGXEditSphere& S : Spheres)
	{
		if (S.R > 0.0f)
		{
			Box += FBox(S.C - FVector(S.R), S.C + FVector(S.R));
		}
	}
	return Box;
}

bool FGXEditIsland::LooksValid(float PlanetRadiusM, float MaxReliefM) const
{
	if (HasPatch())
	{
		if (PatchFace < 0 || PatchFace > 5)
		{
			return false;
		}
		const float Wu = PatchU1 - PatchU0;
		const float Wv = PatchV1 - PatchV0;
		if (!FMath::IsFinite(Wu) || !FMath::IsFinite(Wv) || Wu < 2.0f || Wv < 2.0f
			|| Wu > MaxExtentM + 1.0f || Wv > MaxExtentM + 1.0f)
		{
			return false;
		}
		return true;
	}
	if (Spheres.Num() == 0)
	{
		return false;
	}
	const float MinR = FMath::Max(1.0f, PlanetRadiusM - MaxReliefM - 80.0f);
	const float MaxR = PlanetRadiusM + MaxReliefM + 80.0f;
	for (const FGXEditSphere& S : Spheres)
	{
		if (!FMath::IsFinite(S.R) || S.R < 0.25f || S.R > 200.0f)
		{
			return false;
		}
		if (!FMath::IsFinite(S.C.X) || !FMath::IsFinite(S.C.Y) || !FMath::IsFinite(S.C.Z))
		{
			return false;
		}
		const float Mag = static_cast<float>(S.C.Size());
		if (Mag < MinR || Mag > MaxR)
		{
			return false;
		}
	}
	return true;
}

void FGXEditIsland::Serialize(FArchive& Ar)
{
	int32 N = Spheres.Num();
	Ar << N;
	if (Ar.IsLoading())
	{
		Spheres.SetNum(FMath::Clamp(N, 0, MaxSpheres));
	}
	for (FGXEditSphere& S : Spheres)
	{
		Ar << S.C << S.R;
	}
	int32 Face = PatchFace;
	Ar << Face << PatchU0 << PatchU1 << PatchV0 << PatchV1;
	if (Ar.IsLoading())
	{
		PatchFace = static_cast<int8>(FMath::Clamp(Face, -1, 5));
	}
}

FString FGXEditIsland::DebugString() const
{
	if (HasPatch())
	{
		return FString::Printf(TEXT("sq face=%d u=[%.0f,%.0f] v=[%.0f,%.0f] n=%d"),
			(int32)PatchFace, PatchU0, PatchU1, PatchV0, PatchV1, Spheres.Num());
	}
	if (Spheres.Num() == 0)
	{
		return TEXT("n=0");
	}
	return FString::Printf(TEXT("n=%d r0=%.2f |c0|=%.1f"),
		Spheres.Num(), Spheres[0].R, Spheres[0].C.Size());
}
