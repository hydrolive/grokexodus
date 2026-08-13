"""
Create /Game/Voxel/Materials/M_VoxelTerrain_PBR
Lit triplanar-ish atlas blend. C++ binds AlbedoAtlas / RoughAtlas (4x2 of 512).
"""
import unreal

ASSET = "/Game/Voxel/Materials/M_VoxelTerrain_PBR"
PACKAGE = "/Game/Voxel/Materials"
NAME = "M_VoxelTerrain_PBR"

ALBEDO_CODE = r"""
int id = (int)round(MatId);
if (id <= 0) id = 1;
if (id == 8 || id == 9 || id == 12) id = 2;
if (id == 10) id = 3;
if (id == 11) id = 5;
id = clamp(id, 0, 7);
int rock = 2;
float3 N = normalize(WorldNormal);
float3 Radial = normalize(WorldPos);
float slope = 1.0 - saturate(dot(N, Radial));
float wRock = saturate((slope - SlopeStart) / max(SlopeEnd - SlopeStart, 0.001));
float3 A = abs(N);
A = A / max(A.x + A.y + A.z, 0.0001);
float3 P = WorldPos * Tile;
float2 t0 = float2(frac(P.y), frac(P.z));
float2 t1 = float2(frac(P.x), frac(P.z));
float2 t2 = float2(frac(P.x), frac(P.y));
t0 = t0 * 0.96 + 0.02;
t1 = t1 * 0.96 + 0.02;
t2 = t2 * 0.96 + 0.02;
float2 c0 = float2(fmod((float)id, 4.0), floor((float)id / 4.0));
float2 c1 = float2(fmod((float)rock, 4.0), floor((float)rock / 4.0));
float2 s = float2(0.25, 0.5);
float3 a0 = Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c0 + t0) * s).rgb * A.x
          + Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c0 + t1) * s).rgb * A.y
          + Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c0 + t2) * s).rgb * A.z;
float3 a1 = Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c1 + t0) * s).rgb * A.x
          + Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c1 + t1) * s).rgb * A.y
          + Texture2DSample(AlbedoAtlas, AlbedoAtlasSampler, (c1 + t2) * s).rgb * A.z;
float h0 = Texture2DSample(RoughAtlas, RoughAtlasSampler, (c0 + t2) * s).r;
float h1 = Texture2DSample(RoughAtlas, RoughAtlasSampler, (c1 + t2) * s).r;
float hw = saturate((h1 - h0 + wRock) / max(HeightSharp, 0.05));
return lerp(a0, a1, hw);
"""

ROUGH_CODE = r"""
int id = (int)round(MatId);
if (id <= 0) id = 1;
if (id == 8 || id == 9 || id == 12) id = 2;
if (id == 10) id = 3;
if (id == 11) id = 5;
id = clamp(id, 0, 7);
float3 N = normalize(WorldNormal);
float3 Radial = normalize(WorldPos);
float slope = 1.0 - saturate(dot(N, Radial));
float wRock = saturate((slope - SlopeStart) / max(SlopeEnd - SlopeStart, 0.001));
float3 P = WorldPos * Tile;
float2 t = float2(frac(P.x), frac(P.y)) * 0.96 + 0.02;
float2 c0 = float2(fmod((float)id, 4.0), floor((float)id / 4.0));
float2 c1 = float2(2.0, 0.0);
float2 s = float2(0.25, 0.5);
float r0 = Texture2DSample(RoughAtlas, RoughAtlasSampler, (c0 + t) * s).r;
float r1 = Texture2DSample(RoughAtlas, RoughAtlasSampler, (c1 + t) * s).r;
return lerp(r0, r1, wRock);
"""

NORMAL_CODE = r"""
return normalize(WorldNormal);
"""


def _shading_lit():
    for name in ("MSM_DEFAULT_LIT", "DEFAULT_LIT"):
        if hasattr(unreal.MaterialShadingModel, name):
            return getattr(unreal.MaterialShadingModel, name)
    return None


def _cmot(*names):
    enum = getattr(unreal, "CustomMaterialOutputType", None)
    if not enum:
        return None
    for n in names:
        if hasattr(enum, n):
            return getattr(enum, n)
    return None


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

    lit = _shading_lit()
    if lit is not None:
        mat.set_editor_property("shading_model", lit)
    for prop, val in (
        ("two_sided", False),
        ("b_tangent_space_normal", False),
        ("tangent_space_normal", False),
        ("b_used_with_procedural_mesh", True),
        ("used_with_procedural_mesh", True),
    ):
        try:
            mat.set_editor_property(prop, val)
        except Exception:
            pass

    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    vn = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 140)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 280)

    def scalar(name, default, y):
        n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", default)
        return n

    tile = scalar("TileScale", 0.0045, 420)
    ss = scalar("SlopeStart", 0.32, 540)
    se = scalar("SlopeEnd", 0.72, 660)
    hs = scalar("HeightSharpness", 0.28, 780)

    alb = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1400, -220)
    alb.set_editor_property("parameter_name", "AlbedoAtlas")
    rgh = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1100, -220)
    rgh.set_editor_property("parameter_name", "RoughAtlas")

    mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1100, 280)
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
        inputs = []
        for n in ("WorldPos", "WorldNormal", "MatId", "Tile", "SlopeStart", "SlopeEnd", "HeightSharp", "AlbedoAtlas", "RoughAtlas"):
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            inputs.append(ci)
        try:
            c.set_editor_property("additional_inputs", inputs)
        except Exception as e:
            unreal.log_warning("[GXPBR] additional_inputs %s" % e)
        return c

    c_alb = make_custom(ALBEDO_CODE, 0, _cmot("CMOT_FLOAT3", "FLOAT3"), "GXAlbedo")
    c_rgh = make_custom(ROUGH_CODE, 260, _cmot("CMOT_FLOAT1", "FLOAT1"), "GXRough")
    c_nrm = make_custom(NORMAL_CODE, 500, _cmot("CMOT_FLOAT3", "FLOAT3"), "GXNrm")

    def wire(c):
        mel.connect_material_expressions(wp, "", c, "WorldPos")
        mel.connect_material_expressions(vn, "", c, "WorldNormal")
        mel.connect_material_expressions(mask, "", c, "MatId")
        mel.connect_material_expressions(tile, "", c, "Tile")
        mel.connect_material_expressions(ss, "", c, "SlopeStart")
        mel.connect_material_expressions(se, "", c, "SlopeEnd")
        mel.connect_material_expressions(hs, "", c, "HeightSharp")
        mel.connect_material_expressions(alb, "", c, "AlbedoAtlas")
        mel.connect_material_expressions(rgh, "", c, "RoughAtlas")

    wire(c_alb)
    wire(c_rgh)
    wire(c_nrm)

    mel.connect_material_property(c_alb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(c_rgh, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(c_nrm, "", unreal.MaterialProperty.MP_NORMAL)

    metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 680)
    metal.set_editor_property("r", 0.02)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(ASSET)
    unreal.log("[GXPBR] Created " + ASSET)


if __name__ == "__main__":
    main()
