// Copyright Grok Exodus. All Rights Reserved.

#include "GXEditIsland.h"

bool FGXEditIsland::Contains(const FVector& P) const
{
	for (const FGXEditSphere& S : Spheres)
	{
		if (S.R > 0.0f && FVector::DistSquared(P, S.C) <= S.R * S.R)
		{
			return true;
		}
	}
	return false;
}

bool FGXEditIsland::OverlapsBox(const FBox& Box) const
{
	for (const FGXEditSphere& S : Spheres)
	{
		if (S.R > 0.0f && FVector::DistSquared(Box.GetClosestPointTo(S.C), S.C) <= S.R * S.R)
		{
			return true;
		}
	}
	return false;
}

void FGXEditIsland::Add(const FVector& Center, float RadiusM)
{
	if (RadiusM <= 0.0f)
	{
		return;
	}
	auto Absorb = [](FGXEditSphere& S, const FVector& C, float R)
	{
		const float D = FVector::Dist(S.C, C);
		if (D + R <= S.R + 0.05f)
		{
			return;
		}
		if (D + S.R <= R + 0.05f)
		{
			S.C = C;
			S.R = R;
			return;
		}
		const float NewR = (D + S.R + R) * 0.5f;
		const FVector Dir = (C - S.C).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			S.R = FMath::Max(S.R, R);
			return;
		}
		S.C = S.C + Dir * (NewR - S.R);
		S.R = NewR + 0.05f;
	};
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
		// Do not merge overlapping brushes into one bounding sphere —
		// that 5 m blob punched hillside tiles the cave never filled
		// (0.13.46 black wedges on the slope).
	}
	if (Spheres.Num() >= MaxSpheres)
	{
		int32 Worst = 0;
		float Best = 1.0e12f;
		for (int32 I = 0; I < Spheres.Num(); ++I)
		{
			const float D = FVector::Dist(Center, Spheres[I].C);
			if (D < Best)
			{
				Best = D;
				Worst = I;
			}
		}
		Absorb(Spheres[Worst], Center, RadiusM);
		return;
	}
	FGXEditSphere& N = Spheres.AddDefaulted_GetRef();
	N.C = Center;
	N.R = RadiusM;
}

FBox FGXEditIsland::Bounds() const
{
	FBox B(ForceInit);
	for (const FGXEditSphere& S : Spheres)
	{
		if (S.R > 0.0f)
		{
			B += FBox(S.C - FVector(S.R), S.C + FVector(S.R));
		}
	}
	return B;
}

bool FGXEditIsland::LooksValid(float PlanetRadiusM, float MaxReliefM) const
{
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
}
