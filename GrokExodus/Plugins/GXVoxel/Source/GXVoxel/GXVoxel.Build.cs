using UnrealBuildTool;

public class GXVoxel : ModuleRules
{
	public GXVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GXCore",
			"GXCelestial",
			"ProceduralMeshComponent",
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription"
		});
	}
}
