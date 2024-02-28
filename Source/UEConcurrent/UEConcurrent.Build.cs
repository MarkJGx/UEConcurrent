// Copyright @MarkJGx 2024
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEConcurrent : ModuleRules
{
	public UEConcurrent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp17;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
			}
		);
	}
}