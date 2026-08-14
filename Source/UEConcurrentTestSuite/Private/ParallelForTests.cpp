// Copyright @MarkJGx 2024

#include "CoreMinimal.h"
#include "Concurrent/InlineParallelFor.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParallelForDeterminismTest, "UEConcurrent.ParallelFor.Determinism", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParallelForDeterminismTest::RunTest(const FString& Parameters)
{
	const int32 NumElements = 64;

	TArray<int32> Output;
	Output.SetNum(NumElements);
	UE::Concurrent::InlineParallelFor<EParallelForFlags::ForceSingleThread>(NumElements, [&Output](int32 Index)
	{
		Output[Index] = Index;
	});

	for (int32 Index = 0; Index < NumElements; Index++)
	{
		TestEqual(TEXT("force single thread runs in order"), Output[Index], Index);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParallelForEquivalenceTest, "UEConcurrent.ParallelFor.Equivalence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParallelForEquivalenceTest::RunTest(const FString& Parameters)
{
	const int32 NumElements = 128;

	TArray<int32> Parallel;
	TArray<int32> Serial;
	Parallel.SetNum(NumElements);
	Serial.SetNum(NumElements);

	UE::Concurrent::InlineParallelFor<EParallelForFlags::None>(NumElements, [&Parallel](int32 Index)
	{
		Parallel[Index] = Index * 3;
	});
	UE::Concurrent::InlineParallelFor<EParallelForFlags::ForceSingleThread>(NumElements, [&Serial](int32 Index)
	{
		Serial[Index] = Index * 3;
	});

	TestTrue(TEXT("parallel and serial produce the same result"), Parallel == Serial);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParallelForEachTest, "UEConcurrent.ParallelFor.ForEach", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParallelForEachTest::RunTest(const FString& Parameters)
{
	const int32 NumElements = 64;

	TArray<int32> Source;
	Source.Reserve(NumElements);
	for (int32 Value = 0; Value < NumElements; Value++)
	{
		Source.Add(Value);
	}

	TArray<int32> Doubled;
	Doubled.SetNum(NumElements);
	UE::Concurrent::InlineParallelForEach<EParallelForFlags::None>(Source, [&Doubled](int32 Value)
	{
		Doubled[Value] = Value * 2;
	});

	for (int32 Value = 0; Value < NumElements; Value++)
	{
		TestEqual(TEXT("foreach applied to every element"), Doubled[Value], Value * 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParallelForThreadFlagTest, "UEConcurrent.ParallelFor.ThreadFlag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParallelForThreadFlagTest::RunTest(const FString& Parameters)
{
	const bool bThreadingEnabled = FApp::ShouldUseThreadingForPerformance();
	AddInfo(FString::Printf(TEXT("FApp::ShouldUseThreadingForPerformance() = %s"), bThreadingEnabled ? TEXT("true") : TEXT("false")));

	// Equivalence must hold on both sides of the flag: ParallelForTemplate falls back
	// to serial internally when threading is disabled, so this also covers -onethread runs.
	const int32 NumElements = 128;

	TArray<int32> Parallel;
	TArray<int32> Serial;
	Parallel.SetNum(NumElements);
	Serial.SetNum(NumElements);

	UE::Concurrent::InlineParallelFor<EParallelForFlags::None>(NumElements, [&Parallel](int32 Index)
	{
		Parallel[Index] = Index;
	});
	UE::Concurrent::InlineParallelFor<EParallelForFlags::ForceSingleThread>(NumElements, [&Serial](int32 Index)
	{
		Serial[Index] = Index;
	});

	TestTrue(TEXT("results agree regardless of the threading flag"), Parallel == Serial);

	return true;
}
