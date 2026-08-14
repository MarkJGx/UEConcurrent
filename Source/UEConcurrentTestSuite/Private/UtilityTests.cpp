// Copyright @MarkJGx 2024

#include "CoreMinimal.h"
#include "Utilities/CastToUnderlying.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastToUnderlyingTest, "UEConcurrent.Utility.CastToUnderlying", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastToUnderlyingTest::RunTest(const FString& Parameters)
{
	enum class EFakeEnum : uint8
	{
		ValueA = 1,
		ValueB = 2,
	};

	constexpr uint8 Underlying = CastToUnderlying(EFakeEnum::ValueB);
	static_assert(Underlying == 2, "CastToUnderlying is constexpr");

	TestEqual(TEXT("underlying value round-trips"), static_cast<int32>(Underlying), 2);

	return true;
}
