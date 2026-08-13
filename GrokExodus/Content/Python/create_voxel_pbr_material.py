"""
Create / refresh /Game/Voxel/Materials/M_VoxelTerrain_PBR

GRAPH ONLY. Do not add MaterialExpressionCustom.

UE 5.8 never injects Custom-node input names (WorldNormal, MatId, AlbedoAtlas, …)
into generated Material.ush. That produced the undeclared-identifier flood and
forced the default gray material in PIE.

C++ (FGXTerrainPBR) binds:
  AlbedoAtlas / RoughAtlas   4x2 of 512px Imagine tiles (ids 0-7)
  TileScale, SlopeStart, SlopeEnd

UV0.x is the atlas slot. The mesher remaps ores/bedrock (8-12) into 0-7.

In the editor Output Log:
  py "E:/Github/grokexodus/GrokExodus/Content/Python/create_voxel_pbr_material.py"

Close the material editor first. Success log:
  [GXPBR] OK graph-only /Game/Voxel/Materials/M_VoxelTerrain_PBR custom=0
"""
import unreal

ASSET = "/Game/Voxel/Materials/M_VoxelTerrain_PBR"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_VoxelTerrain_PBR"


def _shading_lit():
    for name in ("MSM_DEFAULT_LIT", "DEFAULT_LIT"):
        if hasattr(unreal.MaterialShadingModel, name):
            return getattr(unreal.MaterialShadingModel, name)
    return None


def _close_editors(asset):
    if not asset:
        return
    try:
        aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if aes:
            aes.close_all_editors_for_asset(asset)
            unreal.log("[GXPBR] closed open editors for the material")
    except Exception as err:
        unreal.log_warning("[GXPBR] close editors: %s" % err)


def _set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception:
        return False


def _expressions(mat):
    try:
        exprs = mat.get_editor_property("expressions")
        return list(exprs) if exprs else []
    except Exception:
        try:
            return list(mat.expressions)
        except Exception:
            return []


def _count_custom(mat):
    n = 0
    for expr in _expressions(mat):
        if expr and type(expr).__name__ == "MaterialExpressionCustom":
            n += 1
    return n


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    mat = None
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
        mat = unreal.EditorAssetLibrary.load_asset(ASSET)
        _close_editors(mat)
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE):
            unreal.EditorAssetLibrary.make_directory(PACKAGE)
        mat = tools.create_asset(NAME, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())

    if not mat:
        unreal.log_error("[GXPBR] could not load or create %s" % ASSET)
        return

    # Wipe the previous graph (including any Custom HLSL) in place so soft
    # references and the content-browser object stay valid.
    try:
        mel.delete_all_material_expressions(mat)
    except Exception as err:
        unreal.log_error("[GXPBR] delete_all_material_expressions failed: %s" % err)
        return

    leftover = _count_custom(mat)
    if leftover:
        unreal.log_error("[GXPBR] %d Custom nodes survived the wipe — aborting" % leftover)
        return

    lit = _shading_lit()
    if lit is not None:
        _set(mat, "shading_model", lit)

    for prop, val in (
        ("blend_mode", unreal.BlendMode.BLEND_OPAQUE),
        ("two_sided", False),
        ("b_tangent_space_normal", False),
        ("tangent_space_normal", False),
        ("b_used_with_procedural_mesh", True),
        ("used_with_procedural_mesh", True),
        ("b_used_with_static_lighting", True),
        ("used_with_static_lighting", True),
        ("automatically_set_usage_in_editor", True),
    ):
        _set(mat, prop, val)

    x0 = -2000

    def node(cls, px, py, desc=""):
        n = mel.create_material_expression(mat, cls, px, py)
        if desc:
            _set(n, "desc", desc)
        return n

    def const(value, px, py, desc=""):
        n = node(unreal.MaterialExpressionConstant, px, py, desc)
        _set(n, "r", float(value))
        return n

    def scalar(name, value, px, py):
        n = node(unreal.MaterialExpressionScalarParameter, px, py, name)
        _set(n, "parameter_name", name)
        _set(n, "default_value", float(value))
        _set(n, "group", "Terrain")
        return n

    def connect(src, src_out, dst, dst_in):
        try:
            mel.connect_material_expressions(src, src_out, dst, dst_in)
            return True
        except Exception as err:
            unreal.log_warning("[GXPBR] connect %s -> %s.%s : %s" % (src_out, type(dst).__name__, dst_in, err))
            return False

    def mul(a, ao, b, bo, px, py, desc=""):
        n = node(unreal.MaterialExpressionMultiply, px, py, desc)
        connect(a, ao, n, "A")
        connect(b, bo, n, "B")
        return n

    def add(a, ao, b, bo, px, py, desc=""):
        n = node(unreal.MaterialExpressionAdd, px, py, desc)
        connect(a, ao, n, "A")
        connect(b, bo, n, "B")
        return n

    def sub(a, ao, b, bo, px, py, desc=""):
        n = node(unreal.MaterialExpressionSubtract, px, py, desc)
        connect(a, ao, n, "A")
        connect(b, bo, n, "B")
        return n

    def div(a, ao, b, bo, px, py, desc=""):
        n = node(unreal.MaterialExpressionDivide, px, py, desc)
        connect(a, ao, n, "A")
        connect(b, bo, n, "B")
        return n

    def sat(a, ao, px, py, desc=""):
        n = node(unreal.MaterialExpressionSaturate, px, py, desc)
        connect(a, ao, n, "")
        return n

    def frac(a, ao, px, py, desc=""):
        n = node(unreal.MaterialExpressionFrac, px, py, desc)
        connect(a, ao, n, "")
        return n

    def floor(a, ao, px, py, desc=""):
        n = node(unreal.MaterialExpressionFloor, px, py, desc)
        connect(a, ao, n, "")
        return n

    def append(a, ao, b, bo, px, py, desc=""):
        n = node(unreal.MaterialExpressionAppendVector, px, py, desc)
        connect(a, ao, n, "A")
        connect(b, bo, n, "B")
        return n

    def mask(a, ao, use_r, use_g, use_b, px, py, desc=""):
        n = node(unreal.MaterialExpressionComponentMask, px, py, desc)
        _set(n, "r", bool(use_r))
        _set(n, "g", bool(use_g))
        _set(n, "b", bool(use_b))
        _set(n, "a", False)
        connect(a, ao, n, "")
        return n

    def tex(param, uv_node, px, py, linear=False):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D, px, py, param)
        _set(n, "parameter_name", param)
        _set(n, "group", "Terrain")
        sampler = None
        if linear:
            sampler = getattr(unreal.MaterialSamplerType, "SAMPLERTYPE_LINEAR_COLOR", None)
        else:
            sampler = getattr(unreal.MaterialSamplerType, "SAMPLERTYPE_COLOR", None)
        if sampler is not None:
            _set(n, "sampler_type", sampler)
        if not connect(uv_node, "", n, "UVs"):
            connect(uv_node, "", n, "Coordinates")
        return n

    # --- inputs -----------------------------------------------------------
    wp = node(unreal.MaterialExpressionWorldPosition, x0, 0, "WorldPos")
    vn = node(unreal.MaterialExpressionVertexNormalWS, x0, 160, "VertexN")
    tc = node(unreal.MaterialExpressionTextureCoordinate, x0, 320, "UV0")
    _set(tc, "coordinate_index", 0)
    vc = node(unreal.MaterialExpressionVertexColor, x0, 460, "VColor")
    tile = scalar("TileScale", 0.0045, x0, 600)
    slope_a = scalar("SlopeStart", 0.32, x0, 720)
    slope_b = scalar("SlopeEnd", 0.72, x0, 840)

    # tiled world XY (spawn is +Z pole — XY is the tangent plane there)
    p = mul(wp, "", tile, "", x0 + 280, 0, "World*Tile")
    p_x = mask(p, "", True, False, False, x0 + 540, -40, "Px")
    p_y = mask(p, "", False, True, False, x0 + 540, 40, "Py")
    lu = frac(p_x, "", x0 + 800, -40, "fracX")
    lv = frac(p_y, "", x0 + 800, 40, "fracY")

    c_096 = const(0.96, x0 + 800, 140, "0.96")
    c_002 = const(0.02, x0 + 800, 200, "0.02")
    pu = add(mul(lu, "", c_096, "", x0 + 1060, -40), "", c_002, "", x0 + 1320, -40, "padU")
    pv = add(mul(lv, "", c_096, "", x0 + 1060, 40), "", c_002, "", x0 + 1320, 40, "padV")

    # atlas cell from UV0.x  (4 columns x 2 rows)
    mat_id = mask(tc, "", True, False, False, x0 + 280, 320, "MatId")
    c_0 = const(0.0, x0 + 280, 400, "0")
    c_7 = const(7.0, x0 + 280, 460, "7")
    id_clamped = node(unreal.MaterialExpressionClamp, x0 + 540, 320, "Id01_7")
    connect(mat_id, "", id_clamped, "")
    connect(c_0, "", id_clamped, "Min")
    connect(c_7, "", id_clamped, "Max")

    c_4 = const(4.0, x0 + 540, 420, "4")
    c_025 = const(0.25, x0 + 540, 480, "0.25")
    c_05 = const(0.5, x0 + 540, 540, "0.5")
    row = floor(mul(id_clamped, "", c_025, "", x0 + 800, 320), "", x0 + 1060, 320, "row")
    col = sub(id_clamped, "", mul(row, "", c_4, "", x0 + 1060, 400), "", x0 + 1320, 320, "col")

    atlas_u = mul(add(col, "", pu, "", x0 + 1580, -40), "", c_025, "", x0 + 1840, -40, "atlasU")
    atlas_v = mul(add(row, "", pv, "", x0 + 1580, 40), "", c_05, "", x0 + 1840, 40, "atlasV")
    atlas_uv = append(atlas_u, "", atlas_v, "", x0 + 2100, 0, "atlasUV")

    # rock cell (2, 0) = T_RockyCliff
    c_2 = const(2.0, x0 + 1320, 520, "2")
    rock_u = mul(add(c_2, "", pu, "", x0 + 1580, 500), "", c_025, "", x0 + 1840, 500, "rockU")
    rock_v = mul(add(c_0, "", pv, "", x0 + 1580, 560), "", c_05, "", x0 + 1840, 560, "rockV")
    rock_uv = append(rock_u, "", rock_v, "", x0 + 2100, 520, "rockUV")

    alb_b = tex("AlbedoAtlas", atlas_uv, x0 + 2360, 0, linear=False)
    alb_r = tex("AlbedoAtlas", rock_uv, x0 + 2360, 220, linear=False)
    rgh_b = tex("RoughAtlas", atlas_uv, x0 + 2360, 440, linear=True)
    rgh_r = tex("RoughAtlas", rock_uv, x0 + 2360, 660, linear=True)

    # wRock = saturate((1 - N·radial - SlopeStart) / (SlopeEnd - SlopeStart))
    radial = node(unreal.MaterialExpressionNormalize, x0 + 280, 720, "radial")
    connect(wp, "", radial, "")
    ndot = node(unreal.MaterialExpressionDotProduct, x0 + 540, 720, "NdotUp")
    connect(vn, "", ndot, "A")
    connect(radial, "", ndot, "B")
    up = sat(ndot, "", x0 + 800, 720, "up")
    one = const(1.0, x0 + 800, 800, "1")
    slope = sub(one, "", up, "", x0 + 1060, 720, "slope")
    numer = sub(slope, "", slope_a, "", x0 + 1320, 720)
    denom = sub(slope_b, "", slope_a, "", x0 + 1320, 800)
    w_rock = sat(div(numer, "", denom, "", x0 + 1580, 720), "", x0 + 1840, 720, "wRock")

    lerp_a = node(unreal.MaterialExpressionLinearInterpolate, x0 + 2700, 80, "AlbedoLerp")
    connect(alb_b, "RGB", lerp_a, "A")
    connect(alb_r, "RGB", lerp_a, "B")
    connect(w_rock, "", lerp_a, "Alpha")

    # keep biome tint visible if the atlas is still the default white
    tint_amt = const(0.45, x0 + 2700, 280, "tintAmt")
    tint_base = const(0.55, x0 + 2700, 340, "tintBase")
    tint = add(mul(vc, "RGB", tint_amt, "", x0 + 2960, 280), "", tint_base, "", x0 + 3220, 280, "tint")
    albedo = mul(lerp_a, "", tint, "", x0 + 3480, 80, "Albedo*Tint")

    lerp_r = node(unreal.MaterialExpressionLinearInterpolate, x0 + 2700, 500, "RoughLerp")
    connect(rgh_b, "R", lerp_r, "A")
    connect(rgh_r, "R", lerp_r, "B")
    connect(w_rock, "", lerp_r, "Alpha")

    def plug(src, src_out, prop):
        try:
            mel.connect_material_property(src, src_out, prop)
        except Exception as err:
            unreal.log_warning("[GXPBR] property %s: %s" % (prop, err))

    plug(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    plug(lerp_r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    plug(vn, "", unreal.MaterialProperty.MP_NORMAL)

    metal = const(0.02, x0 + 3480, 500, "metal")
    plug(metal, "", unreal.MaterialProperty.MP_METALLIC)
    spec = const(0.35, x0 + 3480, 580, "spec")
    try:
        plug(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    except Exception:
        pass

    try:
        mel.layout_material_expressions(mat)
    except Exception:
        pass

    customs = _count_custom(mat)
    if customs:
        unreal.log_error("[GXPBR] ABORT: graph still has %d Custom nodes" % customs)
        return

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(ASSET)
    unreal.log(
        "[GXPBR] OK graph-only %s custom=%d exprs=%d"
        % (ASSET, customs, len(_expressions(mat)))
    )


if __name__ == "__main__":
    main()
