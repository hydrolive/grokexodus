// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GrokExodus : ModuleRules
{
	public GrokExodus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"RHI",
			"RenderCore",
			"ProceduralMeshComponent",
			"ImageWrapper"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"GrokExodus",
			"GrokExodus/Voxel",
			"GrokExodus/Variant_Horror",
			"GrokExodus/Variant_Horror/UI",
			"GrokExodus/Variant_Shooter",
			"GrokExodus/Variant_Shooter/AI",
			"GrokExodus/Variant_Shooter/UI",
			"GrokExodus/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
