// Copyright @MarkJGx 2024

#include "CoreMinimal.h"
#include "Concurrent/ArrayWithThreadsafeAdd.h"
#include "Concurrent/InlineParallelFor.h"
#include "Misc/AutomationTest.h"

struct FMoveCountingElement
{
	static int32 MoveCount;

	int32 Value;

	explicit FMoveCountingElement(int32 InValue) : Value(InValue)
	{
	}

	FMoveCountingElement(FMoveCountingElement&& Other) : Value(Other.Value)
	{
		MoveCount++;
	}

	FMoveCountingElement(const FMoveCountingElement& Other) : Value(Other.Value)
	{
	}

	FMoveCountingElement& operator=(const FMoveCountingElement&) = default;
	FMoveCountingElement& operator=(FMoveCountingElement&&) = default;
};

int32 FMoveCountingElement::MoveCount = 0;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArrayAddIndexNoneTest, "UEConcurrent.ArrayAdd.IndexNone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrayAddIndexNoneTest::RunTest(const FString& Parameters)
{
	TArray<int32> Array;
	const int32 Index = UE::Concurrent::AddToArrayThreadSafe(Array, 42);
	TestTrue(TEXT("no slack reserves INDEX_NONE"), Index == INDEX_NONE);
	TestEqual(TEXT("array is untouched"), Array.Num(), 0);

	Array.Reserve(1);
	const int32 ReservedIndex = UE::Concurrent::AddToArrayThreadSafe(Array, 42);
	TestTrue(TEXT("reserved add succeeds"), ReservedIndex != INDEX_NONE);
	TestEqual(TEXT("added element value"), Array[ReservedIndex], 42);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArrayAddMoveTest, "UEConcurrent.ArrayAdd.Move", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrayAddMoveTest::RunTest(const FString& Parameters)
{
	TArray<FMoveCountingElement> Array;
	Array.Reserve(2);

	FMoveCountingElement::MoveCount = 0;
	FMoveCountingElement MoveSource(7);
	const int32 MoveIndex = UE::Concurrent::AddToArrayThreadSafe(Array, MoveTemp(MoveSource));
	TestEqual(TEXT("rvalue overload moves"), FMoveCountingElement::MoveCount, 1);
	TestEqual(TEXT("moved element value"), Array[MoveIndex].Value, 7);

	FMoveCountingElement::MoveCount = 0;
	FMoveCountingElement CopySource(9);
	const int32 CopyIndex = UE::Concurrent::AddToArrayThreadSafe(Array, CopySource);
	TestEqual(TEXT("lvalue overload copies"), FMoveCountingElement::MoveCount, 0);
	TestEqual(TEXT("copied element value"), Array[CopyIndex].Value, 9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArrayAddParallelTest, "UEConcurrent.ArrayAdd.Parallel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrayAddParallelTest::RunTest(const FString& Parameters)
{
	const int32 NumElements = 100;

	TArray<int32> Values;
	Values.Reserve(NumElements);
	for (int32 Value = 0; Value < NumElements; Value++)
	{
		Values.Add(Value);
	}

	TArray<int32> Result;
	Result.Reserve(NumElements);
	UE::Concurrent::InlineParallelForEach<EParallelForFlags::None>(Values, [&Result](int32 Value)
	{
		const int32 Index = UE::Concurrent::AddToArrayThreadSafe(Result, Value);
		if (Index == INDEX_NONE)
		{
			return;
		}
	});

	TestEqual(TEXT("every parallel add landed"), Result.Num(), NumElements);

	Result.Sort();
	for (int32 Index = 0; Index < NumElements; Index++)
	{
		TestEqual(TEXT("result is a permutation of the inputs"), Result[Index], Index);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArrayAddTArray64Test, "UEConcurrent.ArrayAdd.TArray64", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrayAddTArray64Test::RunTest(const FString& Parameters)
{
	TArray64<int32> Array64;
	Array64.Reserve(2);

	const int64 Index64 = UE::Concurrent::AddToArrayThreadSafe(Array64, 5);
	TestTrue(TEXT("TArray64 add succeeds"), Index64 != INDEX_NONE);
	TestEqual(TEXT("TArray64 element value"), Array64[Index64], 5);

	return true;
}
