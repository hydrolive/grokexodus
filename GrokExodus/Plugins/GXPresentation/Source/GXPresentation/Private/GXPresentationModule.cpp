// Copyright Grok Exodus. All Rights Reserved.

#include "GXPresentation.h"
#include "GXVersion.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogGXPresentation);

static void GXWriteRunningVersionFile()
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("GX_RUNNING_VERSION.txt");
	const FString Body = FString::Printf(
		TEXT("GX %s\n%s\nmodule=GXPresentation\nslate_overlay=1\n"),
		GX_VERSION_STRING, GX_VERSION_DATE);
	FFileHelper::SaveStringToFile(Body, *Path);
}

static FAutoConsoleCommand GCmdGXVersion(
	TEXT("gx.version"),
	TEXT("Print the Grok Exodus build stamp to the log and on screen."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UE_LOG(LogGXPresentation, Warning, TEXT("GX BUILD %s %s"), GX_VERSION_STRING, GX_VERSION_DATE);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow,
				FString::Printf(TEXT("GX %s %s"), GX_VERSION_STRING, GX_VERSION_DATE));
		}
	}));

void FGXPresentationModule::StartupModule()
{
	GXWriteRunningVersionFile();
	UE_LOG(LogGXPresentation, Warning,
		TEXT("********** GX BUILD %s (%s) GXPresentation module **********"),
		GX_VERSION_STRING, GX_VERSION_DATE);
}

void FGXPresentationModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGXPresentationModule, GXPresentation)
