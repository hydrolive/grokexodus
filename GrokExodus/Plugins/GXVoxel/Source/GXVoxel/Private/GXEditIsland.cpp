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
	if (RadiusM <= 0.0f || Center.IsNearlyZero() && RadiusM < 0.01f)
	{
		if (RadiusM <= 0.0f)
		{
			return;
		}
	}
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
		if (D < S.R + RadiusM + 1.25f)
		{
			const FVector Mid = (S.C * S.R + Center * RadiusM) / FMath::Max(S.R + RadiusM, 0.01f);
			S.C = (S.C + Center) * 0.5f;
			S.R = FMath::Max(S.R, D * 0.5f + FMath::Max(S.R, RadiusM));
			(void)Mid;
			return;
		}
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
		FGXEditSphere& S = Spheres[Worst];
		const float D = FVector::Dist(Center, S.C);
		S.C = (S.C + Center) * 0.5f;
		S.R = FMath::Max(S.R, D * 0.5f + FMath::Max(S.R, RadiusM));
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
