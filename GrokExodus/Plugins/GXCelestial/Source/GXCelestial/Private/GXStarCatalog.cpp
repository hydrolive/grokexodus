// Copyright Grok Exodus. All Rights Reserved.

#include "GXStarCatalog.h"

const FGXStar FGXStarCatalog::Stars[FGXStarCatalog::Count] = {
	{ TEXT("Sirius"), 6.752f, -16.72f, -1.46f },
	{ TEXT("Canopus"), 6.399f, -52.70f, -0.74f },
	{ TEXT("RigilKent"), 14.661f, -60.83f, -0.27f },
	{ TEXT("Arcturus"), 14.261f, 19.18f, -0.05f },
	{ TEXT("Vega"), 18.616f, 38.78f, 0.03f },
	{ TEXT("Capella"), 5.278f, 45.99f, 0.08f },
	{ TEXT("Rigel"), 5.242f, -8.20f, 0.13f },
	{ TEXT("Procyon"), 7.655f, 5.22f, 0.38f },
	{ TEXT("Betelgeuse"), 5.919f, 7.41f, 0.50f },
	{ TEXT("Achernar"), 1.629f, -57.24f, 0.46f },
	{ TEXT("Hadar"), 14.064f, -60.37f, 0.61f },
	{ TEXT("Altair"), 19.846f, 8.87f, 0.77f },
	{ TEXT("Acrux"), 12.443f, -63.10f, 0.77f },
	{ TEXT("Aldebaran"), 4.599f, 16.51f, 0.85f },
	{ TEXT("Antares"), 16.490f, -26.43f, 1.09f },
	{ TEXT("Spica"), 13.420f, -11.16f, 1.04f },
	{ TEXT("Pollux"), 7.755f, 28.03f, 1.14f },
	{ TEXT("Fomalhaut"), 22.961f, -29.62f, 1.16f },
	{ TEXT("Deneb"), 20.690f, 45.28f, 1.25f },
	{ TEXT("Mimosa"), 12.795f, -59.69f, 1.25f },
	{ TEXT("Regulus"), 10.139f, 11.97f, 1.35f },
	{ TEXT("Adhara"), 6.977f, -28.97f, 1.50f },
	{ TEXT("Castor"), 7.576f, 31.89f, 1.57f },
	{ TEXT("Shaula"), 17.560f, -37.10f, 1.62f },
	{ TEXT("Bellatrix"), 5.418f, 6.35f, 1.64f },
	{ TEXT("Elnath"), 5.438f, 28.61f, 1.68f },
	{ TEXT("Alnilam"), 5.603f, -1.20f, 1.70f },
	{ TEXT("Alnair"), 22.137f, -46.96f, 1.74f },
	{ TEXT("Alioth"), 12.900f, 55.96f, 1.77f },
	{ TEXT("Alnitak"), 5.679f, -1.94f, 1.77f },
	{ TEXT("Dubhe"), 11.062f, 61.75f, 1.79f },
	{ TEXT("Mirfak"), 3.405f, 49.86f, 1.80f },
	{ TEXT("Wezen"), 7.140f, -26.39f, 1.83f },
	{ TEXT("Alkaid"), 13.792f, 49.31f, 1.86f },
	{ TEXT("Sargas"), 17.622f, -42.99f, 1.87f },
	{ TEXT("Avior"), 8.375f, -59.51f, 1.86f },
	{ TEXT("Menkalinan"), 5.992f, 44.95f, 1.90f },
	{ TEXT("Atria"), 16.811f, -69.03f, 1.92f },
	{ TEXT("Alhena"), 6.628f, 16.40f, 1.93f },
	{ TEXT("Peacock"), 20.427f, -56.74f, 1.94f },
};

FVector3d FGXStarCatalog::Dir(int32 Index)
{
	if (Index >= 0 && Index < Count)
	{
		return Stars[Index].InertialDir();
	}
	const int32 I = FMath::Max(0, Index - Count);
	const double K = static_cast<double>(I) + 0.5;
	const double N = static_cast<double>(FieldCount);
	const double Phi = FMath::Acos(1.0 - 2.0 * K / N);
	const double Theta = PI * (1.0 + FMath::Sqrt(5.0)) * K;
	const double S = FMath::Sin(Phi);
	return FVector3d(S * FMath::Cos(Theta), S * FMath::Sin(Theta), FMath::Cos(Phi));
}

float FGXStarCatalog::Magnitude(int32 Index)
{
	if (Index >= 0 && Index < Count)
	{
		return Stars[Index].Mag;
	}
	const int32 I = FMath::Max(0, Index - Count);
	return 2.4f + static_cast<float>((I * 17) % 29) * 0.11f;
}
