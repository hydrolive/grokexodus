// Copyright Grok Exodus. All Rights Reserved.
// Priority job graph. Workers are POD-only; UObjects stay on the game thread.
#pragma once

#include "CoreMinimal.h"
#include "GXSnapshot.h"

enum class EGXJobPriority : uint8
{
	Collision = 0,
	NearMesh,
	FarMesh,
	Merge,
	Foliage,
	Ephemeris,
	GridNetwork,
	Persistence,
	Count
};

struct FGXJobHandle
{
	uint64 Id = 0;
	FGXGenerationStamp Stamp;
	EGXJobPriority Priority = EGXJobPriority::NearMesh;
	TAtomic<bool> bCompleted{ false };
	TAtomic<bool> bDiscarded{ false };

	bool IsDone() const { return bCompleted.Load(); }
	bool WasDiscarded() const { return bDiscarded.Load(); }
};

/**
 * Thin wrapper over the engine thread pool.
 * Enqueue work with the stamp that was live when the snapshot was published.
 * Apply on the game thread only if handle.Stamp == graph.GetStamp().
 */
class GXCORE_API FGXJobGraph
{
public:
	FGXJobGraph();
	~FGXJobGraph();

	FGXJobGraph(const FGXJobGraph&) = delete;
	FGXJobGraph& operator=(const FGXJobGraph&) = delete;

	/** Current live stamp. Starts at 1. */
	FGXGenerationStamp GetStamp() const
	{
		FGXGenerationStamp S;
		S.Value = LiveStampValue.Load();
		return S;
	}

	/** Call on the game thread after a mutation that invalidates in-flight work. */
	FGXGenerationStamp BumpStamp();

	/**
	 * Run Work on a worker. Stamp is captured for discard.
	 * CompletionCallback (optional) is invoked on the game thread.
	 */
	TSharedRef<FGXJobHandle> Enqueue(
		EGXJobPriority Priority,
		FGXGenerationStamp Stamp,
		TUniqueFunction<void()> Work,
		TUniqueFunction<void()> CompletionOnGameThread = nullptr);

	/** True if a result with Stamp should be applied. */
	bool ShouldApply(FGXGenerationStamp Stamp) const
	{
		return Stamp.IsValid() && Stamp.Value == LiveStampValue.Load();
	}

	int32 NumInFlight() const { return InFlight.Load(); }

	/** Block until in-flight work drains (tests / shutdown). */
	void Flush(float TimeoutSeconds = 10.0f);

private:
	TAtomic<uint64> LiveStampValue{ 1 };
	TAtomic<uint64> NextId{ 1 };
	TAtomic<int32> InFlight{ 0 };
	FCriticalSection HandlesCS;
	TArray<TSharedPtr<FGXJobHandle>> LiveHandles;
};
