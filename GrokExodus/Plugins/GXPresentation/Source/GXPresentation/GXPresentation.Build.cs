using UnrealBuildTool;

public class GXPresentation : ModuleRules
{
	public GXPresentation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
			"GXCore",
			"GXVoxel",
			"GXCelestial",
			"GXConstruct"
		});
	}
}
