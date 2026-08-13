using UnrealBuildTool;

public class GXConstruct : ModuleRules
{
	public GXConstruct(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GXCore",
			"GXCelestial"
		});
	}
}
