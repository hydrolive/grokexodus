"""
Create / recreate /Game/Voxel/Materials/M_VoxelTerrain_VertexColor

Uses UNLIT + Emissive = VertexColor so terrain colors always show (never black from lighting).
Also enables Used with Procedural Mesh.

Run:
  UnrealEditor-Cmd GrokExodus.uproject -ExecutePythonScript=.../create_voxel_terrain_material.py
"""

import unreal


ASSET_PATH = "/Game/Voxel/Materials/M_VoxelTerrain_VertexColor"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_VoxelTerrain_VertexColor"


def main():
    # Delete old asset so we can recreate with correct flags
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.EditorAssetLibrary.delete_asset(ASSET_PATH)
        unreal.log("[VoxelMat] Deleted old material for recreate")

    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE):
        unreal.EditorAssetLibrary.make_directory(PACKAGE)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialFactoryNew()
    mat = asset_tools.create_asset(NAME, PACKAGE, unreal.Material, factory)
    if not mat:
        unreal.log_error("[VoxelMat] create_asset failed")
        return

    mel = unreal.MaterialEditingLibrary
    mel.delete_all_material_expressions(mat)

    # Unlit + emissive vertex color = always visible landscape tints
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception as e:
        unreal.log_warning(f"[VoxelMat] shading_model: {e}")

    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    # Usage flags so ProceduralMeshComponent renders this material
    for prop in (
        "b_used_with_procedural_mesh",
        "used_with_procedural_mesh",
        "bUsedWithProceduralMesh",
    ):
        try:
            mat.set_editor_property(prop, True)
            unreal.log(f"[VoxelMat] set {prop}=True")
            break
        except Exception:
            continue

    # Also try MaterialEditingLibrary API if present
    try:
        if hasattr(mel, "set_material_instance_static_switch_parameter_value"):
            pass
        if hasattr(unreal, "MaterialUsage"):
            # UE5.4+ sometimes uses this pattern via editor utilities
            pass
    except Exception:
        pass

    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -450, 0)
    # Base color (if material falls back to lit)
    mel.connect_material_property(vc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    # Emissive so unlit path shows color even with bad lighting
    mel.connect_material_property(vc, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Slight emissive scale via multiply if available
    try:
        mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 120)
        const = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -450, 160)
        const.set_editor_property("r", 1.0)
        # keep simple: already connected RGB to emissive
    except Exception:
        pass

    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -450, 280)
    rough.set_editor_property("r", 0.9)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -450, 360)
    metal.set_editor_property("r", 0.0)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(ASSET_PATH)
    unreal.log(f"[VoxelMat] Created {ASSET_PATH} (Unlit + VertexColor emissive, two-sided)")


if __name__ == "__main__":
    main()
