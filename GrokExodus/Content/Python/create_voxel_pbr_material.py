"""
Create /Game/Voxel/Materials/M_VoxelTerrain_PBR
Default-lit triplanar Texture2DArray blend (biome + slope rock + height).
"""
import unreal

ASSET = "/Game/Voxel/Materials/M_VoxelTerrain_PBR"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_VoxelTerrain_PBR"

ALBEDO_CODE = r"""
float3 N = normalize(WorldNormal);
float3 Radial = normalize(WorldPos);
float up = saturate(dot(N, Radial));
float slope = 1.0 - up;
float wRock = saturate((slope - SlopeStart) / max(SlopeEnd - SlopeStart, 0.001));
int id = (int)round(MatId);
if (id <= 0) id = 1;
if (id == 8 || id == 9 || id == 12) id = 2;
if (id == 10) id = 3;
if (id == 11) id = 5;
id = clamp(id, 0, 7);
int rock = 2;
float3 A = abs(N);
A /= max(A.x + A.y + A.z, 0.0001);
float3 P = WorldPos * Tile;
float3 uvx = float3(frac(P.y), frac(P.z), id);
float3 uvy = float3(frac(P.x), frac(P.z), id);
float3 uvz = float3(frac(P.x), frac(P.y), id);
float3 rx = float3(frac(P.y), frac(P.z), rock);
float3 ry = float3(frac(P.x), frac(P.z), rock);
float3 rz = float3(frac(P.x), frac(P.y), rock);
float3 c0 = Texture2DArraySample(AlbedoArr, AlbedoArrSampler, uvx).rgb * A.x
          + Texture2DArraySample(AlbedoArr, AlbedoArrSampler, uvy).rgb * A.y
          + Texture2DArraySample(AlbedoArr, AlbedoArrSampler, uvz).rgb * A.z;
float3 c1 = Texture2DArraySample(AlbedoArr, AlbedoArrSampler, rx).rgb * A.x
          + Texture2DArraySample(AlbedoArr, AlbedoArrSampler, ry).rgb * A.y
          + Texture2DArraySample(AlbedoArr, AlbedoArrSampler, rz).rgb * A.z;
float h0 = Texture2DArraySample(RoughArr, RoughArrSampler, uvz).r;
float h1 = Texture2DArraySample(RoughArr, RoughArrSampler, rz).r;
float hw = saturate((h1 - h0 + wRock) / max(HeightSharp, 0.05));
return lerp(c0, c1, hw);
"""

ROUGH_CODE = r"""
float3 N = normalize(WorldNormal);
float3 Radial = normalize(WorldPos);
float slope = 1.0 - saturate(dot(N, Radial));
float wRock = saturate((slope - SlopeStart) / max(SlopeEnd - SlopeStart, 0.001));
int id = (int)round(MatId);
if (id <= 0) id = 1;
if (id == 8 || id == 9 || id == 12) id = 2;
if (id == 10) id = 3;
if (id == 11) id = 5;
id = clamp(id, 0, 7);
float3 A = abs(N);
A /= max(A.x + A.y + A.z, 0.0001);
float3 P = WorldPos * Tile;
float3 uv = float3(frac(P.x), frac(P.y), id);
float3 rv = float3(frac(P.x), frac(P.y), 2);
float r0 = Texture2DArraySample(RoughArr, RoughArrSampler, uv).r;
float r1 = Texture2DArraySample(RoughArr, RoughArrSampler, rv).r;
return lerp(r0, r1, wRock);
"""

NORMAL_CODE = r"""
float3 N = normalize(WorldNormal);
return N;
"""


def _add_input(custom, name):
    inputs = list(custom.get_editor_property("additional_inputs") or [])
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    inputs.append(ci)
    custom.set_editor_property("additional_inputs", inputs)


def _tex_input(custom, name):
    inputs = list(custom.get_editor_property("texture_object_inputs") or []) if hasattr(unreal, "CustomDefine") else None
    # Texture pins come from sampling macros; declare as additional inputs named Tex
    _add_input(custom, name)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
        unreal.EditorAssetLibrary.delete_asset(ASSET)
    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE):
        unreal.EditorAssetLibrary.make_directory(PACKAGE)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        unreal.log_error("[GXPBR] create failed")
        return

    mel = unreal.MaterialEditingLibrary
    mel.delete_all_material_expressions(mat)

    shading = None
    for name in ("MSM_DEFAULT_LIT", "DEFAULT_LIT", "MSM_UNLIT"):
        if hasattr(unreal.MaterialShadingModel, name):
            shading = getattr(unreal.MaterialShadingModel, name)
            break
    if shading is not None:
        mat.set_editor_property("shading_model", shading)
        unreal.log(f"[GXPBR] shading_model={shading}")
    else:
        unreal.log_warning("[GXPBR] no MaterialShadingModel enum found; leaving default")

    try:
        mat.set_editor_property("two_sided", False)
    except Exception:
        pass
    try:
        mat.set_editor_property("b_tangent_space_normal", False)
    except Exception:
        try:
            mat.set_editor_property("tangent_space_normal", False)
        except Exception:
            pass
    for prop in ("b_used_with_procedural_mesh", "used_with_procedural_mesh"):
        try:
            mat.set_editor_property(prop, True)
            break
        except Exception:
            continue

    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    vn = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 160)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 320)

    tile = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 480)
    tile.set_editor_property("parameter_name", "TileScale")
    tile.set_editor_property("default_value", 0.0045)

    ss = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 600)
    ss.set_editor_property("parameter_name", "SlopeStart")
    ss.set_editor_property("default_value", 0.32)

    se = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 720)
    se.set_editor_property("parameter_name", "SlopeEnd")
    se.set_editor_property("default_value", 0.72)

    hs = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 840)
    hs.set_editor_property("parameter_name", "HeightSharpness")
    hs.set_editor_property("default_value", 0.28)

    alb = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1400, -200)
    alb.set_editor_property("parameter_name", "AlbedoArray")
    try:
        alb.set_editor_property("texture_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    except Exception:
        pass

    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1400, -80)
    nrm.set_editor_property("parameter_name", "NormalArray")

    rgh = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1100, -200)
    rgh.set_editor_property("parameter_name", "RoughArray")

    mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1100, 320)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", False)
    mask.set_editor_property("a", False)
    mel.connect_material_expressions(uv, "", mask, "")

    def make_custom(code, y, out_type, desc):
        c = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, y)
        c.set_editor_property("code", code)
        c.set_editor_property("description", desc)
        if out_type is not None:
            try:
                c.set_editor_property("output_type", out_type)
            except Exception:
                pass
        names = [
            "WorldPos", "WorldNormal", "MatId", "Tile", "SlopeStart", "SlopeEnd",
            "HeightSharp", "AlbedoArr", "RoughArr",
        ]
        inputs = []
        for n in names:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            inputs.append(ci)
        try:
            c.set_editor_property("additional_inputs", inputs)
        except Exception as e:
            unreal.log_warning(f"[GXPBR] additional_inputs {e}")
        return c

    def _cmot(*names):
        enum = getattr(unreal, "CustomMaterialOutputType", None)
        if enum is None:
            return None
        for n in names:
            if hasattr(enum, n):
                return getattr(enum, n)
        return None

    c_alb = make_custom(ALBEDO_CODE, 0, _cmot("CMOT_FLOAT3", "FLOAT3"), "GXAlbedoBlend")
    c_rgh = make_custom(ROUGH_CODE, 280, _cmot("CMOT_FLOAT1", "FLOAT1"), "GXRoughBlend")
    c_nrm = make_custom(NORMAL_CODE, 520, _cmot("CMOT_FLOAT3", "FLOAT3"), "GXWorldN")

    def wire(custom):
        mel.connect_material_expressions(wp, "", custom, "WorldPos")
        mel.connect_material_expressions(vn, "", custom, "WorldNormal")
        mel.connect_material_expressions(mask, "", custom, "MatId")
        mel.connect_material_expressions(tile, "", custom, "Tile")
        mel.connect_material_expressions(ss, "", custom, "SlopeStart")
        mel.connect_material_expressions(se, "", custom, "SlopeEnd")
        mel.connect_material_expressions(hs, "", custom, "HeightSharp")
        mel.connect_material_expressions(alb, "", custom, "AlbedoArr")
        mel.connect_material_expressions(rgh, "", custom, "RoughArr")

    wire(c_alb)
    wire(c_rgh)
    wire(c_nrm)

    mel.connect_material_property(c_alb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(c_rgh, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(c_nrm, "", unreal.MaterialProperty.MP_NORMAL)

    metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 700)
    metal.set_editor_property("r", 0.02)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 780)
    spec.set_editor_property("r", 0.35)
    try:
        mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    except Exception:
        pass

    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(ASSET)
    unreal.log("[GXPBR] Created " + ASSET)


if __name__ == "__main__":
    main()
