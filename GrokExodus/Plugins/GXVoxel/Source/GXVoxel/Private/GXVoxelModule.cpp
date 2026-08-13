// Copyright Grok Exodus. All Rights Reserved.

#include "GXVoxel.h"

DEFINE_LOG_CATEGORY(LogGXVoxel);

void FGXVoxelModule::StartupModule()
{
	UE_LOG(LogGXVoxel, Log, TEXT("GXVoxel started"));
}

void FGXVoxelModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGXVoxelModule, GXVoxel)
