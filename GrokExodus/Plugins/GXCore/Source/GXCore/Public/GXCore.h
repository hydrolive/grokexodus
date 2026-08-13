// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGXCore, Log, All);

class FGXCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FGXCoreModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FGXCoreModule>(TEXT("GXCore"));
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("GXCore"));
	}
};
