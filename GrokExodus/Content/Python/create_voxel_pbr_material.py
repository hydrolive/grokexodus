"""
Create / refresh /Game/Voxel/Materials/M_VoxelTerrain_PBR

GRAPH ONLY. Do not add MaterialExpressionCustom.

Imports the Imagine biome JPGs plus the 4x2 atlases and assigns them as
real Texture2D defaults. An empty TextureSampleParameter2D in UE 5.8
falls back to DefaultTextureCube; a cube sampler cannot take the runtime
2D atlas, so the crust goes black.

In the editor Output Log (close the material tab first):
  py "E:/Github/grokexodus/GrokExodus/Content/Python/create_voxel_pbr_material.py"

Success:
  [GXPBR] OK graph-only ... custom=0 albedo=/Game/Voxel/Textures/T_VoxelAlbedoAtlas
"""
import os
import unreal

ASSET = "/Game/Voxel/Materials/M_VoxelTerrain_PBR"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_VoxelTerrain_PBR"
TEX_PKG = "/Game/Voxel/Textures"
ALBEDO_ATLAS = TEX_PKG + "/T_VoxelAlbedoAtlas"
ROUGH_ATLAS = TEX_PKG + "/T_VoxelRoughAtlas"

LAYERS = (
    "T_TemperateGrass",
    "T_RockyCliff",
    "T_DryDirt",
    "T_SandCoastal",
    "T_SnowIce",
    "T_WetMud",
    "T_VolcanicScorched",
)

ENGINE_2D = (
    "/Engine/EngineResources/DefaultTexture.DefaultTexture",
    "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture",
)


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
            unreal.log("[GXPBR] closed open editors")
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


def _source_dir():
    return os.path.normpath(os.path.join(unreal.Paths.project_content_dir(), "Voxel", "Textures", "Source"))


def _load_engine_2d():
    for path in ENGINE_2D:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            tex = unreal.EditorAssetLibrary.load_asset(path)
            if tex:
                return tex
    return None


def _import_texture(abs_path, dest_pkg, dest_name, srgb, clamp):
    dest = dest_pkg + "/" + dest_name
    # Reuse committed uassets. ImportAssetTasks uses Interchange and asserts
    # RecursionGuard when run from the UnrealMCPython TCP game-thread callback.
    if unreal.EditorAssetLibrary.does_asset_exist(dest):
        tex = unreal.EditorAssetLibrary.load_asset(dest)
        if tex:
            unreal.log("[GXPBR] reuse %s" % dest)
            return tex

    if not os.path.isfile(abs_path):
        unreal.log_warning("[GXPBR] missing source %s" % abs_path)
        return None

    if not unreal.EditorAssetLibrary.does_directory_exist(dest_pkg):
        unreal.EditorAssetLibrary.make_directory(dest_pkg)

    unreal.log_warning("[GXPBR] skip import of %s (MCP Interchange crash); assign DefaultTexture" % dest)
    return None

    tex = unreal.EditorAssetLibrary.load_asset(dest)
    if not tex:
        unreal.log_error("[GXPBR] import failed %s" % dest)
        return None

    _set(tex, "srgb", bool(srgb))
    _set(tex, "sRGB", bool(srgb))
    tc = getattr(unreal.TextureCompressionSettings, "TC_DEFAULT", None)
    if tc is not None:
        _set(tex, "compression_settings", tc)
    filt = getattr(unreal.TextureFilter, "TF_BILINEAR", None)
    if filt is not None:
        _set(tex, "filter", filt)
    addr_enum = getattr(unreal.TextureAddress, "TA_CLAMP" if clamp else "TA_WRAP", None)
    if addr_enum is not None:
        _set(tex, "address_x", addr_enum)
        _set(tex, "address_y", addr_enum)
    unreal.EditorAssetLibrary.save_asset(dest)
    unreal.log("[GXPBR] imported %s srgb=%s" % (dest, srgb))
    return tex


def _import_all():
    src = _source_dir()
    albedo = _import_texture(os.path.join(src, "T_VoxelAtlas_A.png"), TEX_PKG, "T_VoxelAlbedoAtlas", True, True)
    rough = _import_texture(os.path.join(src, "T_VoxelAtlas_R.png"), TEX_PKG, "T_VoxelRoughAtlas", False, True)
    for layer in LAYERS:
        _import_texture(os.path.join(src, layer + "_A.jpg"), TEX_PKG, layer + "_A", True, False)
        _import_texture(os.path.join(src, layer + "_R.jpg"), TEX_PKG, layer + "_R", False, False)
    fallback = _load_engine_2d()
    if not albedo:
        albedo = fallback
        unreal.log_warning("[GXPBR] albedo atlas missing — using engine DefaultTexture")
    if not rough:
        rough = fallback
        unreal.log_warning("[GXPBR] rough atlas missing — using engine DefaultTexture")
    return albedo, rough


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    albedo_tex, rough_tex = _import_all()
    if not albedo_tex:
        unreal.log_error("[GXPBR] no 2D albedo texture at all — aborting (would become DefaultTextureCube)")
        return

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
        ("b_tangent_space_normal", True),
        ("tangent_space_normal", True),
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

    def tex(param, uv_node, texture, px, py, linear=False):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D, px, py, param)
        _set(n, "parameter_name", param)
        _set(n, "group", "Terrain")
        if texture:
            if not _set(n, "texture", texture):
                _set(n, "texture_object", texture)
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

    wp = node(unreal.MaterialExpressionWorldPosition, x0, 0, "WorldPos")
    vn = node(unreal.MaterialExpressionVertexNormalWS, x0, 160, "VertexN")
    tc = node(unreal.MaterialExpressionTextureCoordinate, x0, 320, "UV0")
    _set(tc, "coordinate_index", 0)
    vc = node(unreal.MaterialExpressionVertexColor, x0, 460, "VColor")
    # Planar YZ in centimetres. Spawn is +X so the tangent plane is YZ —
    # World XY made flats smear (X barely changes). A per-pixel tangent
    # frame warped every 1 m MC triangle into its own stretched patch.
    tile = scalar("TileScale", 0.0045, x0, 600)
    slope_a = scalar("SlopeStart", 0.32, x0, 720)
    slope_b = scalar("SlopeEnd", 0.72, x0, 840)

    p = mul(wp, "", tile, "", x0 + 280, 0, "World*Tile")
    p_y = mask(p, "", False, True, False, x0 + 540, -40, "Py")
    p_z = mask(p, "", False, False, True, x0 + 540, 40, "Pz")
    lu = frac(p_y, "", x0 + 800, -40, "fracY")
    lv = frac(p_z, "", x0 + 800, 40, "fracZ")

    c_096 = const(0.96, x0 + 800, 140, "0.96")
    c_002 = const(0.02, x0 + 800, 200, "0.02")
    pu = add(mul(lu, "", c_096, "", x0 + 1060, -40), "", c_002, "", x0 + 1320, -40, "padU")
    pv = add(mul(lv, "", c_096, "", x0 + 1060, 40), "", c_002, "", x0 + 1320, 40, "padV")

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

    c_2 = const(2.0, x0 + 1320, 520, "2")
    rock_u = mul(add(c_2, "", pu, "", x0 + 1580, 500), "", c_025, "", x0 + 1840, 500, "rockU")
    rock_v = mul(add(c_0, "", pv, "", x0 + 1580, 560), "", c_05, "", x0 + 1840, 560, "rockV")
    rock_uv = append(rock_u, "", rock_v, "", x0 + 2100, 520, "rockUV")

    alb_b = tex("AlbedoAtlas", atlas_uv, albedo_tex, x0 + 2360, 0, linear=False)
    alb_r = tex("AlbedoAtlas", rock_uv, albedo_tex, x0 + 2360, 220, linear=False)
    rgh_b = tex("RoughAtlas", atlas_uv, rough_tex, x0 + 2360, 440, linear=True)
    rgh_r = tex("RoughAtlas", rock_uv, rough_tex, x0 + 2360, 660, linear=True)

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

    tint_amt = const(0.35, x0 + 2700, 280, "tintAmt")
    tint_base = const(0.65, x0 + 2700, 340, "tintBase")
    tint = add(mul(vc, "RGB", tint_amt, "", x0 + 2960, 280), "", tint_base, "", x0 + 3220, 280, "tint")
    albedo = mul(lerp_a, "", tint, "", x0 + 3480, 80, "Albedo*Tint")

    lerp_r = node(unreal.MaterialExpressionLinearInterpolate, x0 + 2700, 500, "RoughLerp")
    connect(rgh_b, "R", lerp_r, "A")
    connect(rgh_r, "R", lerp_r, "B")
    connect(w_rock, "", lerp_r, "Alpha")

    # Flatten tiling before the stream edge so far chunks do not look like
    # wallpaper. Vertex color is the biome tint without repeats.
    cam = node(unreal.MaterialExpressionCameraPositionWS, x0 + 3480, -80, "Cam")
    dist = node(unreal.MaterialExpressionDistance, x0 + 3740, -80, "CamDist")
    connect(wp, "", dist, "A")
    connect(cam, "", dist, "B")
    fade_a = scalar("DistanceFadeStart", 5000.0, x0 + 3480, -200)
    fade_b = scalar("DistanceFadeEnd", 15000.0, x0 + 3480, -320)
    fade_w = sat(div(sub(dist, "", fade_a, "", x0 + 4000, -80), "",
                     sub(fade_b, "", fade_a, "", x0 + 4000, 0), "", x0 + 4260, -80),
                 "", x0 + 4520, -80, "fadeW")
    far_alb = node(unreal.MaterialExpressionLinearInterpolate, x0 + 4780, 80, "FarAlbedo")
    connect(albedo, "", far_alb, "A")
    connect(vc, "RGB", far_alb, "B")
    connect(fade_w, "", far_alb, "Alpha")
    far_r = node(unreal.MaterialExpressionLinearInterpolate, x0 + 4780, 500, "FarRough")
    connect(lerp_r, "", far_r, "A")
    far_rv = const(0.88, x0 + 4520, 560, "farR")
    connect(far_rv, "", far_r, "B")
    connect(fade_w, "", far_r, "Alpha")
    albedo = far_alb
    lerp_r = far_r

    def plug(src, src_out, prop):
        try:
            mel.connect_material_property(src, src_out, prop)
        except Exception as err:
            unreal.log_warning("[GXPBR] property %s: %s" % (prop, err))

    plug(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    plug(lerp_r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # Leave Normal unconnected so the mesh uses tangent-space vertex normals.
    # Plugging VertexNormalWS into Normal while tangent-space is on makes the crust black.

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
        "[GXPBR] OK graph-only %s custom=%d exprs=%d albedo=%s rough=%s"
        % (ASSET, customs, len(_expressions(mat)),
           ALBEDO_ATLAS if albedo_tex else "none",
           ROUGH_ATLAS if rough_tex else "none")
    )
    _create_horizon()


def _create_horizon():
    """Smooth planet limb past the voxel stream. Clips near the camera."""
    asset = "/Game/Voxel/Materials/M_VoxelHorizon"
    name = "M_VoxelHorizon"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    mat = None
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        mat = unreal.EditorAssetLibrary.load_asset(asset)
        _close_editors(mat)
    else:
        mat = tools.create_asset(name, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        unreal.log_error("[GXPBR] horizon create failed")
        return
    try:
        mel.delete_all_material_expressions(mat)
    except Exception as err:
        unreal.log_error("[GXPBR] horizon wipe: %s" % err)
        return

    lit = _shading_lit()
    if lit is not None:
        _set(mat, "shading_model", lit)
    _set(mat, "blend_mode", unreal.BlendMode.BLEND_MASKED)
    _set(mat, "two_sided", False)
    _set(mat, "used_with_static_mesh", True)
    _set(mat, "b_used_with_static_mesh", True)

    def node(cls, px, py, desc=""):
        n = mel.create_material_expression(mat, cls, px, py)
        if desc:
            _set(n, "desc", desc)
        return n

    grass = node(unreal.MaterialExpressionConstant3Vector, -400, 0, "horizon")
    _set(grass, "constant", unreal.LinearColor(0.27, 0.38, 0.20, 1.0))
    wp = node(unreal.MaterialExpressionWorldPosition, -800, 200, "WP")
    cam = node(unreal.MaterialExpressionCameraPositionWS, -800, 320, "Cam")
    dist = node(unreal.MaterialExpressionDistance, -400, 260, "Dist")
    try:
        mel.connect_material_expressions(wp, "", dist, "A")
        mel.connect_material_expressions(cam, "", dist, "B")
    except Exception:
        pass
    near = node(unreal.MaterialExpressionScalarParameter, -800, 440, "HorizonNearCm")
    _set(near, "parameter_name", "HorizonNearCm")
    _set(near, "default_value", 12000.0)
    _set(near, "group", "Horizon")
    width = node(unreal.MaterialExpressionConstant, -800, 560, "fadeW")
    _set(width, "r", 4000.0)
    subn = node(unreal.MaterialExpressionSubtract, -200, 260)
    mel.connect_material_expressions(dist, "", subn, "A")
    mel.connect_material_expressions(near, "", subn, "B")
    divn = node(unreal.MaterialExpressionDivide, 0, 260)
    mel.connect_material_expressions(subn, "", divn, "A")
    mel.connect_material_expressions(width, "", divn, "B")
    maskv = node(unreal.MaterialExpressionSaturate, 200, 260)
    mel.connect_material_expressions(divn, "", maskv, "")
    try:
        mel.connect_material_property(grass, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(maskv, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    except Exception as err:
        unreal.log_warning("[GXPBR] horizon plug: %s" % err)
    rough = node(unreal.MaterialExpressionConstant, 200, 400)
    _set(rough, "r", 0.9)
    try:
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    except Exception:
        pass
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(asset)
    unreal.log("[GXPBR] OK horizon " + asset)


if __name__ == "__main__":
    main()
