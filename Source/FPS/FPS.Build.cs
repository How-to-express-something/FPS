// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FPS : ModuleRules
{
	public FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags","PhysicsCore","UMG", "OnlineSubsystem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "OnlineSubsystemUtils" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd"); // Opt-in real PIE travel regression.
		}
	}
}
