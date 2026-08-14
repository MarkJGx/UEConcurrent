// Copyright @MarkJGx 2024

using UnrealBuildTool;

public class UEConcurrentTestSuite : ModuleRules
{
	public UEConcurrentTestSuite(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"UEConcurrent",
			}
		);
	}
}
