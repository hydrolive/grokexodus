// Copyright Grok Exodus. All Rights Reserved.
// Immutable, thread-safe snapshots. Workers never see live UObjects.
#pragma once

#include "CoreMinimal.h"

/**
 * Monotonic generation stamp. Game thread bumps on mutation.
 * Worker results with a stale stamp are discarded on apply.
 */
struct FGXGenerationStamp
{
	uint64 Value = 0;

	FORCEINLINE bool IsValid() const { return Value != 0; }
	FORCEINLINE bool operator==(const FGXGenerationStamp& O) const { return Value == O.Value; }
	FORCEINLINE bool operator!=(const FGXGenerationStamp& O) const { return Value != O.Value; }
};

/**
 * Base for copy-on-write snapshots published to workers.
 * Payload subclasses add POD / immutable containers only.
 */
class GXCORE_API FGXSnapshotBase : public TSharedFromThis<FGXSnapshotBase, ESPMode::ThreadSafe>
{
public:
	virtual ~FGXSnapshotBase() = default;

	FGXGenerationStamp Stamp;

	bool IsCurrent(FGXGenerationStamp Live) const { return Stamp.IsValid() && Stamp == Live; }
};

using FGXSnapshotRef = TSharedRef<const FGXSnapshotBase, ESPMode::ThreadSafe>;
using FGXSnapshotPtr = TSharedPtr<const FGXSnapshotBase, ESPMode::ThreadSafe>;
