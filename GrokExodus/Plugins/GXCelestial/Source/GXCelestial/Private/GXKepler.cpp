// Copyright Grok Exodus. All Rights Reserved.

#include "GXKepler.h"

double FGXKepler::SolveEccentricAnomaly(double MeanAnomaly, double Eccentricity, int32 MaxIters)
{
	// Wrap M into [-pi, pi] for faster convergence
	double M = FMath::Fmod(MeanAnomaly + PI, 2.0 * PI);
	if (M < 0.0)
	{
		M += 2.0 * PI;
	}
	M -= PI;

	double E = M;
	if (Eccentricity > 0.8)
	{
		E = PI;
	}
	for (int32 I = 0; I < MaxIters; ++I)
	{
		const double F = E - Eccentricity * FMath::Sin(E) - M;
		const double Dp = 1.0 - Eccentricity * FMath::Cos(E);
		if (FMath::Abs(Dp) < 1e-15)
		{
			break;
		}
		const double DE = F / Dp;
		E -= DE;
		if (FMath::Abs(DE) < 1e-12)
		{
			break;
		}
	}
	return E;
}

double FGXKepler::MeanAnomalyAt(const FGXKeplerElements& E, double UniversalTime)
{
	const double N = FMath::Sqrt(E.Mu / (E.SemiMajorAxis * E.SemiMajorAxis * E.SemiMajorAxis));
	return E.MeanAnomaly0 + N * (UniversalTime - E.Epoch);
}

double FGXKepler::CircularVelocity(double Mu, double Radius)
{
	return FMath::Sqrt(Mu / FMath::Max(Radius, 1.0));
}

double FGXKepler::EscapeVelocity(double Mu, double Radius)
{
	return FMath::Sqrt(2.0 * Mu / FMath::Max(Radius, 1.0));
}

double FGXKepler::Period(double Mu, double A)
{
	return 2.0 * PI * FMath::Sqrt((A * A * A) / Mu);
}

double FGXKepler::SphereOfInfluence(double SemiMajorAboutParent, double BodyMass, double ParentMass)
{
	if (ParentMass <= 0.0)
	{
		return 0.0;
	}
	return SemiMajorAboutParent * FMath::Pow(BodyMass / ParentMass, 0.4);
}

FGXOrbitalState FGXKepler::Evaluate(const FGXKeplerElements& El, double UniversalTime)
{
	FGXOrbitalState Out;
	Out.bHyperbolic = El.Eccentricity >= 1.0;
	Out.Period = Out.bHyperbolic ? 0.0 : Period(El.Mu, El.SemiMajorAxis);
	Out.Periapsis = El.SemiMajorAxis * (1.0 - El.Eccentricity);
	Out.Apoapsis = Out.bHyperbolic ? 0.0 : El.SemiMajorAxis * (1.0 + El.Eccentricity);

	if (Out.bHyperbolic)
	{
		// v1: treat as escape — caller should be integrating
		Out.Position = FVector3d(El.SemiMajorAxis, 0, 0);
		return Out;
	}

	const double M = MeanAnomalyAt(El, UniversalTime);
	const double Ecc = SolveEccentricAnomaly(M, El.Eccentricity);
	Out.EccentricAnomaly = Ecc;

	const double CosE = FMath::Cos(Ecc);
	const double SinE = FMath::Sin(Ecc);
	const double Sqrt1e = FMath::Sqrt(FMath::Max(0.0, 1.0 - El.Eccentricity * El.Eccentricity));
	const double TrueAnom = FMath::Atan2(Sqrt1e * SinE, CosE - El.Eccentricity);
	Out.TrueAnomaly = TrueAnom;

	const double R = El.SemiMajorAxis * (1.0 - El.Eccentricity * CosE);
	const double PF_X = R * FMath::Cos(TrueAnom);
	const double PF_Y = R * FMath::Sin(TrueAnom);

	const double H = FMath::Sqrt(El.Mu * El.SemiMajorAxis * (1.0 - El.Eccentricity * El.Eccentricity));
	const double Vr = (El.Mu / H) * El.Eccentricity * FMath::Sin(TrueAnom);
	const double Vt = H / R;
	const double PV_X = Vr * FMath::Cos(TrueAnom) - Vt * FMath::Sin(TrueAnom);
	const double PV_Y = Vr * FMath::Sin(TrueAnom) + Vt * FMath::Cos(TrueAnom);

	const double CosO = FMath::Cos(El.LongAscNode);
	const double SinO = FMath::Sin(El.LongAscNode);
	const double CosW = FMath::Cos(El.ArgPeriapsis);
	const double SinW = FMath::Sin(El.ArgPeriapsis);
	const double CosI = FMath::Cos(El.Inclination);
	const double SinI = FMath::Sin(El.Inclination);

	const FVector3d PAxis(
		CosO * CosW - SinO * SinW * CosI,
		SinO * CosW + CosO * SinW * CosI,
		SinW * SinI);
	const FVector3d QAxis(
		-CosO * SinW - SinO * CosW * CosI,
		-SinO * SinW + CosO * CosW * CosI,
		CosW * SinI);

	Out.Position = PAxis * PF_X + QAxis * PF_Y;
	Out.Velocity = PAxis * PV_X + QAxis * PV_Y;
	return Out;
}

FGXKeplerElements FGXKepler::FromState(const FVector3d& R, const FVector3d& V, double Mu, double UniversalTime)
{
	FGXKeplerElements E;
	E.Mu = Mu;
	E.Epoch = UniversalTime;

	const FVector3d H = FVector3d::CrossProduct(R, V);
	const double Rmag = R.Size();
	const double V2 = V.SizeSquared();
	const FVector3d Evec = (FVector3d::CrossProduct(V, H) / Mu) - (R / Rmag);
	E.Eccentricity = Evec.Size();

	const double Energy = V2 * 0.5 - Mu / Rmag;
	if (Energy >= 0.0 || E.Eccentricity >= 1.0)
	{
		E.Eccentricity = FMath::Max(E.Eccentricity, 1.0);
		E.SemiMajorAxis = -Mu / (2.0 * FMath::Max(Energy, 1e-12));
		return E;
	}

	E.SemiMajorAxis = -Mu / (2.0 * Energy);
	E.Inclination = FMath::Acos(FMath::Clamp(H.Z / H.Size(), -1.0, 1.0));

	const FVector3d K(0, 0, 1);
	const FVector3d N = FVector3d::CrossProduct(K, H);
	const double Nmag = N.Size();
	if (Nmag > 1e-12)
	{
		E.LongAscNode = FMath::Atan2(N.Y, N.X);
		E.ArgPeriapsis = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(N, Evec) / (Nmag * E.Eccentricity), -1.0, 1.0));
		if (Evec.Z < 0.0)
		{
			E.ArgPeriapsis = 2.0 * PI - E.ArgPeriapsis;
		}
	}

	const double CosNu = FMath::Clamp(FVector3d::DotProduct(Evec, R) / (E.Eccentricity * Rmag), -1.0, 1.0);
	double Nu = FMath::Acos(CosNu);
	if (FVector3d::DotProduct(R, V) < 0.0)
	{
		Nu = 2.0 * PI - Nu;
	}
	const double CosEcc = (E.Eccentricity + FMath::Cos(Nu)) / (1.0 + E.Eccentricity * FMath::Cos(Nu));
	double EccA = FMath::Acos(FMath::Clamp(CosEcc, -1.0, 1.0));
	if (Nu > PI)
	{
		EccA = 2.0 * PI - EccA;
	}
	E.MeanAnomaly0 = EccA - E.Eccentricity * FMath::Sin(EccA);
	return E;
}

double FGXKepler::ClosedOrbitError(const FGXKeplerElements& E, int32 Periods)
{
	const double T = Period(E.Mu, E.SemiMajorAxis);
	const FGXOrbitalState A = Evaluate(E, E.Epoch);
	const FGXOrbitalState B = Evaluate(E, E.Epoch + T * static_cast<double>(Periods));
	return (B.Position - A.Position).Size();
}
