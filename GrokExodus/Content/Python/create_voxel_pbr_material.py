"""
Create / refresh /Game/Voxel/Materials/M_VoxelTerrain_PBR

GRAPH ONLY. Do not add MaterialExpressionCustom.

Landscape-grade dual-scale triplanar: near detail + macro scale so color
remains at distance without wallpaper. Rock tiles coarser than grass.
Do not fade to vertex color.

Imports Imagine atlases as Texture2D defaults (never leave samples unbound —
UE 5.8 falls back to DefaultTextureCube).

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

    # Landscape-grade dual scale (cm). Near grass ~2.2 m. Macro ~20 m.
    # Rock tiles coarser than grass so mountains keep color without wallpaper.
    tile = scalar("TileScale", 0.0028, x0, 600)
    macro = scalar("MacroScale", 0.12, x0, 720)
    rock_mul = scalar("RockTileMul", 0.28, x0, 840)
    rock_mac = scalar("RockMacroMul", 0.07, x0, 960)
    fade_a = scalar("DistanceFadeStart", 3500.0, x0, 1080)
    fade_b = scalar("DistanceFadeEnd", 22000.0, x0, 1200)
    slope_a = scalar("SlopeStart", 0.18, x0, 1320)
    slope_b = scalar("SlopeMid", 0.38, x0, 1440)
    slope_c = scalar("SlopeEnd", 0.70, x0, 1560)

    # Dominant-axis planar UVs so cliffs are not stretched YZ wood grain.
    px = mask(wp, "", True, False, False, x0 + 280, -120, "Px")
    py = mask(wp, "", False, True, False, x0 + 280, -40, "Py")
    pz = mask(wp, "", False, False, True, x0 + 280, 40, "Pz")

    c_096 = const(0.82, x0 + 280, 200, "0.82")
    c_002 = const(0.09, x0 + 280, 260, "0.09")
    c_0 = const(0.0, x0 + 280, 400, "0")
    c_2 = const(2.0, x0 + 280, 460, "2")
    c_4 = const(4.0, x0 + 280, 520, "4")
    c_025 = const(0.25, x0 + 280, 580, "0.25")
    c_05 = const(0.5, x0 + 280, 640, "0.5")
    c_7 = const(7.0, x0 + 280, 700, "7")

    mat_id = mask(tc, "", True, False, False, x0 + 560, 320, "MatId")
    id_clamped = node(unreal.MaterialExpressionClamp, x0 + 820, 320, "Id01_7")
    connect(mat_id, "", id_clamped, "")
    connect(c_0, "", id_clamped, "Min")
    connect(c_7, "", id_clamped, "Max")
    row = floor(mul(id_clamped, "", c_025, "", x0 + 1080, 320), "", x0 + 1340, 320, "row")
    col = sub(id_clamped, "", mul(row, "", c_4, "", x0 + 1340, 400), "", x0 + 1600, 320, "col")

    radial = node(unreal.MaterialExpressionNormalize, x0 + 560, 800, "radial")
    connect(wp, "", radial, "")

    def atlas_sample_axes(param, texture, tnode, cell_u, cell_v, ua, va, ox, oy, linear):
        fu = frac(mul(ua, "", tnode, "", ox, oy), "", ox + 240, oy)
        fv = frac(mul(va, "", tnode, "", ox, oy + 70), "", ox + 240, oy + 70)
        pu = add(mul(fu, "", c_096, "", ox + 480, oy), "", c_002, "", ox + 720, oy)
        pv = add(mul(fv, "", c_096, "", ox + 480, oy + 70), "", c_002, "", ox + 720, oy + 70)
        au = mul(add(cell_u, "", pu, "", ox + 960, oy), "", c_025, "", ox + 1200, oy)
        av = mul(add(cell_v, "", pv, "", ox + 960, oy + 70), "", c_05, "", ox + 1200, oy + 70)
        uv = append(au, "", av, "", ox + 1440, oy)
        return tex(param, uv, texture, ox + 1680, oy, linear)

    def atlas_sample(param, texture, tnode, cell_u, cell_v, ox, oy, linear):
        return atlas_sample_axes(param, texture, tnode, cell_u, cell_v, py, pz, ox, oy, linear)

    nx = mask(vn, "", True, False, False, x0 + 560, 1180, "Nx")
    ny = mask(vn, "", False, True, False, x0 + 560, 1260, "Ny")
    nz = mask(vn, "", False, False, True, x0 + 560, 1340, "Nz")
    ax = node(unreal.MaterialExpressionAbs, x0 + 800, 1180, "aX")
    connect(nx, "", ax, "")
    ay = node(unreal.MaterialExpressionAbs, x0 + 800, 1260, "aY")
    connect(ny, "", ay, "")
    az = node(unreal.MaterialExpressionAbs, x0 + 800, 1340, "aZ")
    connect(nz, "", az, "")
    wsum = add(add(ax, "", ay, "", x0 + 1040, 1220), "", az, "", x0 + 1280, 1220, "wSum")
    wx = div(ax, "", wsum, "", x0 + 1520, 1180, "wX")
    wy = div(ay, "", wsum, "", x0 + 1520, 1260, "wY")
    wz = div(az, "", wsum, "", x0 + 1520, 1340, "wZ")

    def triplanar_albedo(param, texture, tnode, cell_u, cell_v, ox, oy):
        s_yz = atlas_sample_axes(param, texture, tnode, cell_u, cell_v, py, pz, ox, oy, False)
        s_xz = atlas_sample_axes(param, texture, tnode, cell_u, cell_v, px, pz, ox, oy + 220, False)
        s_xy = atlas_sample_axes(param, texture, tnode, cell_u, cell_v, px, py, ox, oy + 440, False)
        a = add(mul(s_yz, "", wx, "", ox + 2000, oy), "", mul(s_xz, "", wy, "", ox + 2000, oy + 220), "", ox + 2300, oy)
        return add(a, "", mul(s_xy, "", wz, "", ox + 2000, oy + 440), "", ox + 2600, oy, "tri")

    far_tile = mul(tile, "", macro, "", x0 + 560, 0, "FarTile")
    rock_near = mul(tile, "", rock_mul, "", x0 + 560, 80, "RockNear")
    rock_far = mul(tile, "", rock_mac, "", x0 + 560, 160, "RockFar")

    grass_n = triplanar_albedo("AlbedoAtlas", albedo_tex, tile, col, row, x0 + 1900, -80)
    grass_f = atlas_sample("AlbedoAtlas", albedo_tex, far_tile, col, row, x0 + 1900, 160, False)
    rock_n = triplanar_albedo("AlbedoAtlas", albedo_tex, rock_near, c_2, c_0, x0 + 1900, 400)
    rock_f = atlas_sample("AlbedoAtlas", albedo_tex, rock_far, c_2, c_0, x0 + 1900, 640, False)
    dirt_col = const(3.0, x0 + 1700, 800, "dirtCol")
    dirt_n = atlas_sample("AlbedoAtlas", albedo_tex, tile, dirt_col, c_0, x0 + 1900, 800, False)

    # Macro as variation up close (breaks repeats) then take over with distance.
    half = const(0.5, x0 + 7500, -200, "0.5")
    var_amt = const(0.4, x0 + 7500, -140, "varAmt")
    grass_var = add(mul(grass_f, "", half, "", x0 + 7760, -200), "", half, "", x0 + 8020, -200)
    grass_detail = node(unreal.MaterialExpressionLinearInterpolate, x0 + 8280, -80, "GrassVar")
    connect(grass_n, "", grass_detail, "A")
    connect(mul(grass_n, "", grass_var, "", x0 + 8020, -40), "", grass_detail, "B")
    connect(var_amt, "", grass_detail, "Alpha")
    rock_var = add(mul(rock_f, "", half, "", x0 + 7760, 200), "", half, "", x0 + 8020, 200)
    rock_detail = node(unreal.MaterialExpressionLinearInterpolate, x0 + 8280, 280, "RockVar")
    connect(rock_n, "", rock_detail, "A")
    connect(mul(rock_n, "", rock_var, "", x0 + 8020, 320), "", rock_detail, "B")
    connect(var_amt, "", rock_detail, "Alpha")

    cam = node(unreal.MaterialExpressionCameraPositionWS, x0 + 7500, 600, "Cam")
    dist = node(unreal.MaterialExpressionDistance, x0 + 7760, 600, "CamDist")
    connect(wp, "", dist, "A")
    connect(cam, "", dist, "B")
    fade_w = sat(div(sub(dist, "", fade_a, "", x0 + 8020, 560), "",
                     sub(fade_b, "", fade_a, "", x0 + 8020, 640), "", x0 + 8280, 600),
                 "", x0 + 8540, 600, "fadeW")

    grass = node(unreal.MaterialExpressionLinearInterpolate, x0 + 8800, -80, "GrassLOD")
    connect(grass_detail, "", grass, "A")
    connect(grass_f, "", grass, "B")
    connect(fade_w, "", grass, "Alpha")
    rock = node(unreal.MaterialExpressionLinearInterpolate, x0 + 8800, 280, "RockLOD")
    connect(rock_detail, "", rock, "A")
    connect(rock_f, "", rock, "B")
    connect(fade_w, "", rock, "Alpha")

    ndot = node(unreal.MaterialExpressionDotProduct, x0 + 560, 1000, "NdotUp")
    connect(vn, "", ndot, "A")
    connect(radial, "", ndot, "B")
    one = const(1.0, x0 + 820, 1080, "1")
    slope = sub(one, "", sat(ndot, "", x0 + 820, 1000), "", x0 + 1080, 1000, "slope")
    w_dirt = sat(div(sub(slope, "", slope_a, "", x0 + 1340, 1000), "",
                     sub(slope_b, "", slope_a, "", x0 + 1340, 1080), "", x0 + 1600, 1000),
                 "", x0 + 1860, 1000, "wDirt")
    w_rock = sat(div(sub(slope, "", slope_b, "", x0 + 1340, 1160), "",
                     sub(slope_c, "", slope_b, "", x0 + 1340, 1240), "", x0 + 1600, 1160),
                 "", x0 + 1860, 1160, "wRock")

    dirt = dirt_n
    skirt = node(unreal.MaterialExpressionLinearInterpolate, x0 + 9100, -40, "GrassDirt")
    connect(grass, "", skirt, "A")
    connect(dirt, "", skirt, "B")
    connect(w_dirt, "", skirt, "Alpha")
    albedo = node(unreal.MaterialExpressionLinearInterpolate, x0 + 9100, 80, "Albedo")
    connect(skirt, "", albedo, "A")
    connect(rock, "", albedo, "B")
    connect(w_rock, "", albedo, "Alpha")

    # Light biome tint only — do not replace texture at distance.
    tint = add(mul(vc, "RGB", const(0.18, x0 + 9100, 280), "", x0 + 9360, 280),
               "", const(0.82, x0 + 9100, 340), "", x0 + 9620, 280, "tint")
    albedo = mul(albedo, "", tint, "", x0 + 9880, 80, "Albedo*Tint")

    rgh_n = atlas_sample("RoughAtlas", rough_tex, tile, col, row, x0 + 1900, 880, True)
    rgh_rn = atlas_sample("RoughAtlas", rough_tex, rock_near, c_2, c_0, x0 + 1900, 1100, True)
    lerp_r = node(unreal.MaterialExpressionLinearInterpolate, x0 + 9100, 500, "Rough")
    connect(rgh_n, "", lerp_r, "A")
    connect(rgh_rn, "", lerp_r, "B")
    connect(w_rock, "", lerp_r, "Alpha")

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
    _create_horizon_far()


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


def _create_horizon_far():
    """Lit vertex colors for clipmap rings. No 2 m atlas on 72 m triangles."""
    asset = "/Game/Voxel/Materials/M_VoxelHorizonFar"
    name = "M_VoxelHorizonFar"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    mat = None
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        mat = unreal.EditorAssetLibrary.load_asset(asset)
        _close_editors(mat)
    else:
        mat = tools.create_asset(name, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        return
    try:
        mel.delete_all_material_expressions(mat)
    except Exception:
        return
    lit = _shading_lit()
    if lit is not None:
        _set(mat, "shading_model", lit)
    _set(mat, "two_sided", False)
    _set(mat, "used_with_static_mesh", True)
    _set(mat, "used_with_procedural_mesh", True)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -300, 0)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 160)
    _set(rough, "r", 0.88)
    try:
        mel.connect_material_property(vc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    except Exception as err:
        unreal.log_warning("[GXPBR] far plug: %s" % err)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(asset)
    unreal.log("[GXPBR] OK horizon far " + asset)


if __name__ == "__main__":
    main()
