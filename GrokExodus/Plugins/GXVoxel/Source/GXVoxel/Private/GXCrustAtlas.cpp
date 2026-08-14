// Copyright Grok Exodus. All Rights Reserved.

#include "GXCrustAtlas.h"
#include "GXVoxel.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe> FGXCrustAtlas::Build(
	const FGXPlanetStampParams& Params,
	const FVector& InOriginDir,
	float HalfExtentM,
	float InCellM)
{
	TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe> Atlas = MakeShared<FGXCrustAtlas, ESPMode::ThreadSafe>();
	const double T0 = FPlatformTime::Seconds();
	FVector Dir = InOriginDir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector(1, 0, 0);
	}
	Atlas->OriginDir = Dir;
	Dir.FindBestAxisVectors(Atlas->Tangent, Atlas->Bitangent);
	Atlas->PlanetRadius = Params.Radius;
	Atlas->CellM = FMath::Max(InCellM, 1.0f);
	const int32 Half = FMath::Max(8, FMath::CeilToInt(HalfExtentM / Atlas->CellM));
	Atlas->Dim = Half * 2 + 1;
	const int32 N = Atlas->Dim * Atlas->Dim;
	Atlas->Height.SetNumUninitialized(N);
	Atlas->Material.SetNumUninitialized(N);

	const FGXSphereStamp Stamp(Params);
	const float R = Params.Radius;
	for (int32 J = 0; J < Atlas->Dim; ++J)
	{
		const float V = (static_cast<float>(J - Half)) * Atlas->CellM;
		for (int32 I = 0; I < Atlas->Dim; ++I)
		{
			const float U = (static_cast<float>(I - Half)) * Atlas->CellM;
			FVector SampleDir = (Dir * R + Atlas->Tangent * U + Atlas->Bitangent * V).GetSafeNormal();
			if (SampleDir.IsNearlyZero())
			{
				SampleDir = Dir;
			}
			const FVector3f D(SampleDir.X, SampleDir.Y, SampleDir.Z);
			const float H = Stamp.SampleHeightDisplacement(D) - Stamp.SampleScarCarveMeters(D);
			const int32 Idx = I + J * Atlas->Dim;
			Atlas->Height[Idx] = H;
			const FVector3d Surf(SampleDir.X * (R + H - 0.4), SampleDir.Y * (R + H - 0.4), SampleDir.Z * (R + H - 0.4));
			Atlas->Material[Idx] = static_cast<uint8>(FMath::Clamp(Stamp.SampleMaterial(Surf, 1.0f), 0, 255));
		}
	}
	Atlas->BuildSeconds = FPlatformTime::Seconds() - T0;
	UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustAtlas built %dx%d in %.2fs (cell=%.2fm extent=%.0fm)"),
		Atlas->Dim, Atlas->Dim, Atlas->BuildSeconds, Atlas->CellM, HalfExtentM);
	return Atlas;
}

bool FGXCrustAtlas::ContainsDir(const FVector& UnitDir) const
{
	if (Dim < 2 || Height.Num() != Dim * Dim)
	{
		return false;
	}
	const FVector OnSphere = UnitDir.GetSafeNormal() * PlanetRadius;
	const FVector Center = OriginDir * PlanetRadius;
	const float U = FVector::DotProduct(OnSphere - Center, Tangent);
	const float V = FVector::DotProduct(OnSphere - Center, Bitangent);
	const float Half = 0.5f * static_cast<float>(Dim - 1) * CellM;
	return FMath::Abs(U) <= Half && FMath::Abs(V) <= Half;
}

float FGXCrustAtlas::SampleHeight(const FVector3f& UnitDir) const
{
	float D = 0.0f;
	uint8 M = 0;
	const FVector3d P(UnitDir.X * PlanetRadius, UnitDir.Y * PlanetRadius, UnitDir.Z * PlanetRadius);
	if (TrySample(P, D, M))
	{
		return static_cast<float>(PlanetRadius + D - P.Size());
	}
	return 0.0f;
}

bool FGXCrustAtlas::TrySample(const FVector3d& PlanetLocalM, float& OutDensity, uint8& OutMat) const
{
	if (Dim < 2 || Height.Num() != Dim * Dim)
	{
		return false;
	}
	const double Len = FMath::Sqrt(PlanetLocalM.X * PlanetLocalM.X + PlanetLocalM.Y * PlanetLocalM.Y + PlanetLocalM.Z * PlanetLocalM.Z);
	if (Len < 1.0)
	{
		return false;
	}
	const FVector Dir(PlanetLocalM.X / Len, PlanetLocalM.Y / Len, PlanetLocalM.Z / Len);
	const FVector OnSphere = Dir * PlanetRadius;
	const FVector Center = OriginDir * PlanetRadius;
	const float U = FVector::DotProduct(OnSphere - Center, Tangent);
	const float V = FVector::DotProduct(OnSphere - Center, Bitangent);
	const float HalfCells = 0.5f * static_cast<float>(Dim - 1);
	const float Fx = U / CellM + HalfCells;
	const float Fy = V / CellM + HalfCells;
	if (Fx < 0.0f || Fy < 0.0f || Fx > static_cast<float>(Dim - 1) || Fy > static_cast<float>(Dim - 1))
	{
		return false;
	}
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(Fx), 0, Dim - 2);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Fy), 0, Dim - 2);
	const float Tx = Fx - static_cast<float>(X0);
	const float Ty = Fy - static_cast<float>(Y0);
	const int32 I00 = X0 + Y0 * Dim;
	const int32 I10 = I00 + 1;
	const int32 I01 = I00 + Dim;
	const int32 I11 = I01 + 1;
	const float H = FMath::Lerp(
		FMath::Lerp(Height[I00], Height[I10], Tx),
		FMath::Lerp(Height[I01], Height[I11], Tx),
		Ty);
	OutDensity = static_cast<float>((static_cast<double>(PlanetRadius) + H) - Len);
	const float W00 = (1.0f - Tx) * (1.0f - Ty);
	const float W10 = Tx * (1.0f - Ty);
	const float W01 = (1.0f - Tx) * Ty;
	const float W11 = Tx * Ty;
	uint8 Best = Material[I00];
	float BestW = W00;
	if (W10 > BestW) { BestW = W10; Best = Material[I10]; }
	if (W01 > BestW) { BestW = W01; Best = Material[I01]; }
	if (W11 > BestW) { Best = Material[I11]; }
	OutMat = Best;
	return true;
}

bool FGXCrustAtlas::SaveToFile(const FString& Path) const
{
	TArray<uint8> Buf;
	auto W = [&](const void* D, int32 S)
	{
		const int32 Off = Buf.Num();
		Buf.AddUninitialized(S);
		FMemory::Memcpy(Buf.GetData() + Off, D, S);
	};
	const uint32 Mag = 0x314C5847; // GXL1
	const int32 Ver = 1;
	W(&Mag, 4); W(&Ver, 4);
	W(&PlanetRadius, 4); W(&CellM, 4); W(&Dim, 4);
	W(&OriginDir, sizeof(FVector));
	W(&Tangent, sizeof(FVector));
	W(&Bitangent, sizeof(FVector));
	if (Height.Num() != Dim * Dim || Material.Num() != Dim * Dim)
	{
		return false;
	}
	W(Height.GetData(), Height.Num() * sizeof(float));
	W(Material.GetData(), Material.Num());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(Buf, *Path);
}

TSharedPtr<FGXCrustAtlas, ESPMode::ThreadSafe> FGXCrustAtlas::LoadFromFile(const FString& Path)
{
	TArray<uint8> Buf;
	if (!FFileHelper::LoadFileToArray(Buf, *Path) || Buf.Num() < 40)
	{
		return nullptr;
	}
	int32 Off = 0;
	auto R = [&](void* D, int32 S) -> bool
	{
		if (Off + S > Buf.Num()) return false;
		FMemory::Memcpy(D, Buf.GetData() + Off, S);
		Off += S;
		return true;
	};
	TSharedRef<FGXCrustAtlas, ESPMode::ThreadSafe> Atlas = MakeShared<FGXCrustAtlas, ESPMode::ThreadSafe>();
	uint32 Mag = 0;
	int32 Ver = 0;
	if (!R(&Mag, 4) || !R(&Ver, 4) || Mag != 0x314C5847 || Ver != 1)
	{
		return nullptr;
	}
	if (!R(&Atlas->PlanetRadius, 4) || !R(&Atlas->CellM, 4) || !R(&Atlas->Dim, 4))
	{
		return nullptr;
	}
	if (!R(&Atlas->OriginDir, sizeof(FVector)) || !R(&Atlas->Tangent, sizeof(FVector)) || !R(&Atlas->Bitangent, sizeof(FVector)))
	{
		return nullptr;
	}
	if (Atlas->Dim < 2 || Atlas->Dim > 1024)
	{
		return nullptr;
	}
	const int32 N = Atlas->Dim * Atlas->Dim;
	Atlas->Height.SetNumUninitialized(N);
	Atlas->Material.SetNumUninitialized(N);
	if (!R(Atlas->Height.GetData(), N * sizeof(float)) || !R(Atlas->Material.GetData(), N))
	{
		return nullptr;
	}
	UE_LOG(LogGXVoxel, Warning, TEXT("GXCrustAtlas loaded %s (%dx%d)"), *Path, Atlas->Dim, Atlas->Dim);
	return Atlas;
}
