using UnrealBuildTool;

public class GXCelestial : ModuleRules
{
	public GXCelestial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GXCore"
		});
	}
}
