// Copyright Grok Exodus. All Rights Reserved.

#include "GXJobGraph.h"
#include "GXCore.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

FGXJobGraph::FGXJobGraph() = default;

FGXJobGraph::~FGXJobGraph()
{
	Flush(5.0f);
}

FGXGenerationStamp FGXJobGraph::BumpStamp()
{
	check(IsInGameThread());
	uint64 Next = LiveStampValue.IncrementExchange() + 1;
	if (Next == 0)
	{
		LiveStampValue.Store(1);
		Next = 1;
	}
	FGXGenerationStamp S;
	S.Value = Next;
	return S;
}

TSharedRef<FGXJobHandle> FGXJobGraph::Enqueue(
	EGXJobPriority Priority,
	FGXGenerationStamp Stamp,
	TUniqueFunction<void()> Work,
	TUniqueFunction<void()> CompletionOnGameThread)
{
	TSharedRef<FGXJobHandle> Handle = MakeShared<FGXJobHandle>();
	Handle->Id = NextId.IncrementExchange() + 1;
	Handle->Stamp = Stamp;
	Handle->Priority = Priority;

	{
		FScopeLock Lock(&HandlesCS);
		LiveHandles.Add(Handle);
	}

	InFlight.IncrementExchange();

	// Capture by value: shared handle + moved work. No UObject*.
	Async(EAsyncExecution::ThreadPool,
		[this, Handle, Work = MoveTemp(Work), Completion = MoveTemp(CompletionOnGameThread)]() mutable
		{
			const uint64 Live = LiveStampValue.Load();
			if (Handle->Stamp.Value != Live)
			{
				Handle->bDiscarded.Store(true);
			}
			else if (Work)
			{
				Work();
			}

			Handle->bCompleted.Store(true);
			InFlight.DecrementExchange();

			if (Completion)
			{
				TUniqueFunction<void()> Done = MoveTemp(Completion);
				const bool bStale = (Handle->Stamp.Value != LiveStampValue.Load());
				AsyncTask(ENamedThreads::GameThread, [Handle, Done = MoveTemp(Done), bStale]() mutable
				{
					if (bStale || Handle->Stamp.Value == 0)
					{
						Handle->bDiscarded.Store(true);
						return;
					}
					Done();
				});
			}
		});

	(void)Priority; // reserved for a real priority queue in a later PR
	return Handle;
}

void FGXJobGraph::Flush(float TimeoutSeconds)
{
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (InFlight.Load() > 0 && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.001f);
	}
	if (InFlight.Load() > 0)
	{
		UE_LOG(LogGXCore, Warning, TEXT("FGXJobGraph::Flush timed out with %d jobs in flight"), InFlight.Load());
	}
}
