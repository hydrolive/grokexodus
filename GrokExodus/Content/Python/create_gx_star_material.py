"""Create /Game/Voxel/Materials/M_GXStar and M_GXSunLambert."""
import unreal

PACKAGE = "/Game/Voxel/Materials"


def _set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception:
        return False


def _close(mat):
    try:
        aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if aes and mat:
            aes.close_all_editors_for_asset(mat)
    except Exception:
        pass


def _load_or_create(name):
    asset = PACKAGE + "/" + name
    mat = None
    for path in (asset, asset + "." + name):
        try:
            mat = unreal.load_asset(path)
        except Exception:
            mat = None
        if mat:
            break
        try:
            mat = unreal.EditorAssetLibrary.load_asset(path)
        except Exception:
            mat = None
        if mat:
            break
    if mat:
        _close(mat)
        return mat
    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE):
        unreal.EditorAssetLibrary.make_directory(PACKAGE)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())


def _usage(mat):
    _set(mat, "shading_model", unreal.MaterialShadingModel.UNLIT)
    _set(mat, "blend_mode", unreal.BlendMode.OPAQUE)
    _set(mat, "two_sided", False)
    for prop in (
        "used_with_static_mesh",
        "used_with_procedural_mesh",
        "used_with_instanced_static_meshes",
        "b_used_with_static_mesh",
        "b_used_with_procedural_mesh",
        "b_used_with_instanced_static_meshes",
    ):
        _set(mat, prop, True)


def _make_star():
    mat = _load_or_create("M_GXStar")
    if not mat:
        unreal.log_error("[GXSTAR] failed M_GXStar")
        return
    mel = unreal.MaterialEditingLibrary
    try:
        mel.delete_all_material_expressions(mat)
    except Exception:
        pass
    _usage(mat)
    const = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    _set(const, "constant", unreal.LinearColor(1.0, 0.97, 0.92, 1.0))
    try:
        mel.connect_material_property(const, "", unreal.MaterialProperty.MP_EMISSIVECOLOR)
    except Exception as err:
        unreal.log_warning("[GXSTAR] emit: %s" % err)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[GXSTAR] OK /Game/Voxel/Materials/M_GXStar")


def _make_sun_lambert():
    mat = _load_or_create("M_GXSunLambert")
    if not mat:
        unreal.log_error("[GXSTAR] failed M_GXSunLambert")
        return
    mel = unreal.MaterialEditingLibrary
    try:
        mel.delete_all_material_expressions(mat)
    except Exception:
        pass
    _usage(mat)
    nrm = mel.create_material_expression(
        mat, unreal.MaterialExpressionPixelNormalWS, -560, 0)
    sun = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -560, 140)
    _set(sun, "parameter_name", "SunDir")
    _set(sun, "default_value", unreal.LinearColor(1.0, 0.0, 0.0, 0.0))
    albedo = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -560, 300)
    _set(albedo, "parameter_name", "Albedo")
    _set(albedo, "default_value", unreal.LinearColor(0.70, 0.70, 0.66, 1.0))
    dot = mel.create_material_expression(
        mat, unreal.MaterialExpressionDotProduct, -320, 40)
    try:
        mel.connect_material_expressions(nrm, "", dot, "A")
        mel.connect_material_expressions(sun, "", dot, "B")
    except Exception as err:
        unreal.log_warning("[GXSTAR] dot: %s" % err)
    sat = mel.create_material_expression(
        mat, unreal.MaterialExpressionSaturate, -180, 40)
    try:
        mel.connect_material_expressions(dot, "", sat, "")
    except Exception:
        try:
            mel.connect_material_expressions(dot, "", sat, "Input")
        except Exception as err:
            unreal.log_warning("[GXSTAR] sat: %s" % err)
    mul = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -40, 80)
    try:
        mel.connect_material_expressions(sat, "", mul, "A")
        mel.connect_material_expressions(albedo, "RGB", mul, "B")
    except Exception as err:
        unreal.log_warning("[GXSTAR] mul: %s" % err)
    amb = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -180, 240)
    _set(amb, "constant", unreal.LinearColor(0.018, 0.018, 0.022, 1.0))
    add = mel.create_material_expression(
        mat, unreal.MaterialExpressionAdd, 80, 80)
    try:
        mel.connect_material_expressions(mul, "", add, "A")
        mel.connect_material_expressions(amb, "", add, "B")
    except Exception as err:
        unreal.log_warning("[GXSTAR] add: %s" % err)
    try:
        mel.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVECOLOR)
    except Exception as err:
        unreal.log_warning("[GXSTAR] emit: %s" % err)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[GXSTAR] OK /Game/Voxel/Materials/M_GXSunLambert")


def main():
    _make_star()
    _make_sun_lambert()


if __name__ == "__main__":
    main()
