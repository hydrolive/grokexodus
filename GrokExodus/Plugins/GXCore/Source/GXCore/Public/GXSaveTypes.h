// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** On-disk `.gxsav` identity. Payload layout versions independently. */
namespace GXSave
{
	/** ASCII "GXS1" little-endian. */
	inline constexpr uint32 Magic = 0x31535847u;
	inline constexpr uint32 Version = 1;

	struct FGXSaveHeader
	{
		uint32 Magic = Magic;
		uint32 Version = Version;
		uint32 UniverseSeed = 0;
		double UniversalTime = 0.0;
		uint64 ActiveBodyId = 0;
		uint32 Reserved[4] = { 0, 0, 0, 0 };
	};

	static_assert(sizeof(FGXSaveHeader) == 48, "Keep the save header POD and stable.");
}
