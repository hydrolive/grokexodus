"""Create /Game/Voxel/Materials/M_GXStar — unlit emissive white dots."""
import unreal

ASSET = "/Game/Voxel/Materials/M_GXStar"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_GXStar"


def _set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception:
        return False


def main():
    asset_lib = unreal.EditorAssetLibrary
    if asset_lib.does_asset_exist(ASSET):
        mat = asset_lib.load_asset(ASSET)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.MaterialFactoryNew()
        mat = tools.create_asset(NAME, PACKAGE, unreal.Material, factory)
    if not mat:
        unreal.log_error("[GXSTAR] failed to create material")
        return
    try:
        aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if aes:
            aes.close_all_editors_for_asset(mat)
    except Exception:
        pass
    _set(mat, "shading_model", unreal.MaterialShadingModel.UNLIT)
    _set(mat, "blend_mode", unreal.BlendMode.OPAQUE)
    _set(mat, "two_sided", True)
    try:
        mat.set_editor_property("b_used_with_instanced_static_meshes", True)
        mat.set_editor_property("b_used_with_static_lighting", False)
    except Exception:
        pass
    # Constant emissive
    try:
        const = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
        const.set_editor_property("constant", unreal.LinearColor(1.0, 0.97, 0.92, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            const, "", unreal.MaterialProperty.MP_EMISSIVECOLOR)
    except Exception as err:
        unreal.log_warning("[GXSTAR] emit wire: %s" % err)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[GXSTAR] OK %s" % ASSET)


if __name__ == "__main__":
    main()
