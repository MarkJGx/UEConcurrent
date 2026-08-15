// Copyright @MarkJGx 2024

#include "CoreMinimal.h"
#include "Concurrent/ReadWriteLock.h"
#include "HAL/Thread.h"
#include "Misc/AutomationTest.h"

#include "Containers/Map.h"

struct FNonDefaultConstructible
{
	int32 Value;

	explicit FNonDefaultConstructible(int32 InValue) : Value(InValue)
	{
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockSerialTest, "UEConcurrent.Lock.Serial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockSerialTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<TMap<int32, int32>> Lock;

	Lock.ReadWriteLocked([](auto& Map)
	{
		Map.Add(1, 10);
		Map.Add(2, 20);
	});

	const int32 Count = Lock.ReadLocked_Get([](const auto& Map)
	{
		return Map.Num();
	});
	TestEqual(TEXT("locked read sees both writes"), Count, 2);

	const int32 UnsafeCount = Lock.ReadUnsafe_Get([](const auto& Map)
	{
		return Map.Num();
	});
	TestEqual(TEXT("unsafe read sees both writes"), UnsafeCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockForwardingCtorTest, "UEConcurrent.Lock.ForwardingCtor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockForwardingCtorTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<FNonDefaultConstructible> Lock(42);

	const int32 Value = Lock.ReadLocked_Get([](const FNonDefaultConstructible& Type)
	{
		return Type.Value;
	});
	TestEqual(TEXT("forwarded constructor argument"), Value, 42);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockRecursionTest, "UEConcurrent.Lock.Recursion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockRecursionTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<TMap<int32, int32>> Lock;

	Lock.ReadWriteLocked([this, &Lock](auto& Map)
	{
		Map.Add(1, 10);

		Lock.ReadLocked([this](const auto& ReadOnlyMap)
		{
			TestEqual(TEXT("recursive read sees the write"), ReadOnlyMap.Num(), 1);
		});

		const int32 NestedCount = Lock.ReadWriteLocked_Get([](auto& WriteMap)
		{
			return WriteMap.Num();
		});
		TestEqual(TEXT("recursive write _Get sees both writes"), NestedCount, 1);

		Lock.ReadWriteLocked([](auto& WriteMap)
		{
			WriteMap.Add(2, 20);
		});
	});

	const int32 Count = Lock.ReadLocked_Get([](const auto& Map)
	{
		return Map.Num();
	});
	TestEqual(TEXT("all recursive writes visible"), Count, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMultithreadedTest, "UEConcurrent.Lock.Multithreaded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockMultithreadedTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<int32> Lock;

	const int32 NumWorkers = 8;
	const int32 IncrementsPerWorker = 1000;

	TArray<FThread> Workers;
	Workers.Reserve(NumWorkers);
	for (int32 WorkerIndex = 0; WorkerIndex < NumWorkers; WorkerIndex++)
	{
		Workers.Emplace(*FString::Printf(TEXT("UEConcurrent.Lock.Worker.%d"), WorkerIndex), [&Lock, IncrementsPerWorker]()
		{
			for (int32 IncrementIndex = 0; IncrementIndex < IncrementsPerWorker; IncrementIndex++)
			{
				Lock.ReadWriteLocked([](int32& Value)
				{
					Value++;
				});
			}
		});
	}

	for (FThread& Worker : Workers)
	{
		Worker.Join();
	}

	int32 FinalValue = 0;
	Lock.ReadLocked([&FinalValue](const int32& Value)
	{
		FinalValue = Value;
	});
	TestEqual(TEXT("every increment is visible"), FinalValue, NumWorkers * IncrementsPerWorker);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMoveOperatorsTest, "UEConcurrent.Lock.MoveOperators", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockMoveOperatorsTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<TArray<int32>> Lock;

	Lock.ReadWriteLocked([](auto& Array)
	{
		TArray<int32> Other;
		Other.Add(1);
		Other.Add(2);
		Other.Add(3);
		Array = MoveTemp(Other);
	});

	const int32 Count = Lock.ReadLocked_Get([](const auto& Array)
	{
		return Array.Num();
	});
	TestEqual(TEXT("moved array is visible through the lock"), Count, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockDisabledChecksTest, "UEConcurrent.Lock.DisabledChecks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockDisabledChecksTest::RunTest(const FString& Parameters)
{
	UE::Concurrent::TReadWriteLock<int32, false> Lock;
	Lock.ReadWriteLocked([](int32& Value)
	{
		Value = 5;
	});
	TestEqual(TEXT("disabled-check lock still synchronizes"), Lock.ReadLocked_Get([](const int32& Value)
	{
		return Value;
	}), 5);

	int32 Field = 0;
	UE::Concurrent::TReadWriteLockView<int32, false> View(Field);
	View.ReadWriteLocked([](int32& Value)
	{
		Value = 7;
	});
	TestEqual(TEXT("disabled-check view still synchronizes"), Field, 7);

#if !DO_CHECK
	UE::Concurrent::TReadWriteLock<int32, true> ExplicitCheckedLock;
	ExplicitCheckedLock.ReadUnsafe([](const int32&)
	{
	});
	ExplicitCheckedLock.ReadUnsafe_Get([](const int32& Value)
	{
		return Value;
	});
	ExplicitCheckedLock.ReadLocked([](const int32&)
	{
	});
	ExplicitCheckedLock.ReadLocked_Get([](const int32& Value)
	{
		return Value;
	});
	ExplicitCheckedLock.ReadWriteLocked([](int32& Value)
	{
		Value = 9;
	});
	TestEqual(TEXT("explicit checked lock compiles and writes in shipping"), ExplicitCheckedLock.ReadLocked_Get([](const int32& Value)
	{
		return Value;
	}), 9);

	int32 ExplicitCheckedField = 0;
	UE::Concurrent::TReadWriteLockView<int32, true> ExplicitCheckedView(ExplicitCheckedField);
	ExplicitCheckedView.ReadWriteLocked([](int32& Value)
	{
		Value = 13;
	});
	TestEqual(TEXT("explicit checked view compiles and writes in shipping"), ExplicitCheckedField, 13);
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockViewTest, "UEConcurrent.Lock.View", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockViewTest::RunTest(const FString& Parameters)
{
	int32 Field = 0;
	UE::Concurrent::TReadWriteLockView<int32> View(Field);

	View.ReadWriteLocked([](int32& Value)
	{
		Value = 11;
	});

	const int32 Value = View.ReadLocked_Get([](const int32& ReadOnlyValue)
	{
		return ReadOnlyValue;
	});
	TestEqual(TEXT("view write is visible"), Value, 11);

	UE::Concurrent::TReadWriteLockView<const int32> ReadOnlyView(Field);
	const int32 ReadOnlyValue = ReadOnlyView.ReadUnsafe_Get([](const int32& ReadOnlyValue)
	{
		return ReadOnlyValue;
	});
	TestEqual(TEXT("const view reads"), ReadOnlyValue, 11);

	return true;
}
