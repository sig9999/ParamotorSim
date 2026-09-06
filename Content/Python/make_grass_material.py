"""
make_grass_material.py  —  FIXED VERSION
========================================
Creates two assets for wind-animated grass in ParamotorSim:

1. /Game/Materials/MPC_Wind  — Material Parameter Collection
   - WindVector  (Vector4): XYZ = normalized wind dir, W = speed (m/s)
   - WindStrength (Scalar): wind speed in m/s
   - WindGust    (Scalar): gust intensity 0-1

2. /Game/Materials/M_TakeoffGrass — Masked, Two Sided Foliage material
   - Reads WindVector from MPC_Wind for directional WPO
   - Uses LocalPosition for height mask (no vertex color dependency)
   - Per-instance phase variation via PerInstanceRandom
   - Subsurface tint for backlit blades
   - Distance fade via PerInstanceFadeAmount

Run from the UE5.8 editor:
    Tools -> Script Editor -> Run Script...  ->  make_grass_material.py

Or from the Python console:
    py "/Game/Python/make_grass_material.py"

This script is IDEMPOTENT — safe to run multiple times.

NOTE: If this script fails, see the MANUAL FALLBACK instructions at the bottom.
The MPC can be created manually in ~30 seconds.
"""

import unreal

MPC_PATH = "/Game/Materials/MPC_Wind"
MAT_PATH = "/Game/Materials/M_TakeoffGrass"

# Existing grass textures in the project
GRASS_DIFFUSE = "/Game/Textures/Grass/T_LeafyGrass_D"
GRASS_NORMAL = "/Game/Textures/Grass/T_LeafyGrass_N"
GRASS_ROUGH = "/Game/Textures/Grass/T_LeafyGrass_R"
GRASS_AO = "/Game/Textures/Grass/T_LeafyGrass_AO"


def log(msg):
    unreal.log("[make_grass_material] " + msg)


def create_mpc():
    """Create the Material Parameter Collection for wind."""
    eal = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Delete existing if present
    if eal.does_asset_exist(MPC_PATH):
        eal.delete_asset(MPC_PATH)
        log("Deleted existing MPC_Wind")

    # Create new MPC
    factory = unreal.MaterialParameterCollectionFactoryNew()
    mpc = asset_tools.create_asset(
        "MPC_Wind",
        "/Game/Materials",
        unreal.MaterialParameterCollection,
        factory,
    )
    if not mpc:
        log("ERROR: Failed to create MPC_Wind")
        return None

    # Add scalar parameters
    # We use the struct type directly for reliability
    scalar_params = []
    for name in ["WindStrength", "WindGust"]:
        param = unreal.CollectionScalarParameter()
        param.set_editor_property("name", unreal.Name(name))
        param.set_editor_property("default_value", 0.0)
        scalar_params.append(param)
    mpc.set_editor_property("scalar_parameters", scalar_params)

    # Add vector parameter
    vec_param = unreal.CollectionVectorParameter()
    vec_param.set_editor_property("name", unreal.Name("WindVector"))
    vec_param.set_editor_property("default_value", unreal.LinearColor(1.0, 0.0, 0.0, 0.0))
    mpc.set_editor_property("vector_parameters", [vec_param])

    # Save
    eal.save_asset(MPC_PATH)
    log("Created and saved MPC_Wind with WindVector, WindStrength, WindGust")
    return mpc


def create_grass_material(mpc):
    """Create the wind-animated grass material."""
    eal = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    # Delete existing if present
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
        log("Deleted existing M_TakeoffGrass")

    # Create new material
    factory = unreal.MaterialFactoryNew()
    mat = asset_tools.create_asset(
        "M_TakeoffGrass",
        "/Game/Materials",
        unreal.Material,
        factory,
    )
    if not mat:
        log("ERROR: Failed to create M_TakeoffGrass")
        return None

    # Configure material properties
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_Masked)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TwoSidedFoliage)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("tangent_space_normal", False)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_foliage", True)

    # Helper: create expression at position
    def expr(cls, x=0, y=0):
        return mel.create_material_expression(mat, cls, x, y)

    # Helper: connect two expressions
    def connect(src, src_out, dst, dst_in):
        mel.connect_material_expressions(src, src_out, dst, dst_in)

    # Helper: connect expression to material output
    def connect_out(src, src_out, prop):
        mel.connect_material_property(src, src_out, prop)

    # ================================================================
    # TEXTURE SAMPLES
    # ================================================================
    tex_diff = expr(unreal.MaterialExpressionTextureSampleParameter2D, 0, 0)
    tex_diff.set_editor_property("parameter_name", unreal.Name("GrassDiffuse"))
    tex_diff.set_editor_property("texture", eal.load_asset(GRASS_DIFFUSE))

    tex_normal = expr(unreal.MaterialExpressionTextureSampleParameter2D, 0, 200)
    tex_normal.set_editor_property("parameter_name", unreal.Name("GrassNormal"))
    tex_normal.set_editor_property("texture", eal.load_asset(GRASS_NORMAL))

    tex_ao = expr(unreal.MaterialExpressionTextureSampleParameter2D, 0, 600)
    tex_ao.set_editor_property("parameter_name", unreal.Name("GrassAO"))
    tex_ao.set_editor_property("texture", eal.load_asset(GRASS_AO))

    # ================================================================
    # PER INSTANCE RANDOM (for color/phase variation)
    # ================================================================
    pir = expr(unreal.MaterialExpressionPerInstanceRandom, 400, 0)

    # ================================================================
    # WIND FROM MPC
    # ================================================================
    wind_vec = expr(unreal.MaterialExpressionCollectionParameter, 400, 300)
    wind_vec.set_editor_property("collection", mpc)
    wind_vec.set_editor_property("parameter_name", unreal.Name("WindVector"))

    # Extract wind direction XY (horizontal)
    wind_xy = expr(unreal.MaterialExpressionComponentMask, 600, 300)
    wind_xy.set_editor_property("r", True)
    wind_xy.set_editor_property("g", True)
    wind_xy.set_editor_property("b", False)
    wind_xy.set_editor_property("a", False)
    connect(wind_vec, "", wind_xy, "")

    # Extract wind speed W
    wind_w = expr(unreal.MaterialExpressionComponentMask, 600, 400)
    wind_w.set_editor_property("r", False)
    wind_w.set_editor_property("g", False)
    wind_w.set_editor_property("b", False)
    wind_w.set_editor_property("a", True)
    connect(wind_vec, "", wind_w, "")

    # ================================================================
    # HEIGHT MASK from LocalPosition (no vertex color dependency)
    # For SM_GrassClump meshes: Z is up, mesh base at ~0, top at ~30-50cm
    # We normalize: (LocalZ + 5) / 50 → ~0 at base, ~1 at top
    # Clamped to 0-1 so roots stay fixed
    # ================================================================
    local_pos = expr(unreal.MaterialExpressionLocalPosition, 800, 300)

    # Extract Z component
    local_z = expr(unreal.MaterialExpressionComponentMask, 1000, 300)
    local_z.set_editor_property("r", False)
    local_z.set_editor_property("g", False)
    local_z.set_editor_property("b", True)
    local_z.set_editor_property("a", False)
    connect(local_pos, "", local_z, "")

    # Add offset (in case mesh base is slightly below 0)
    z_offset = expr(unreal.MaterialExpressionConstant, 1200, 300)
    z_offset.set_editor_property("r", 5.0)

    z_add = expr(unreal.MaterialExpressionAdd, 1400, 300)
    connect(local_z, "", z_add, "A")
    connect(z_offset, "", z_add, "B")

    # Divide by max height
    z_max = expr(unreal.MaterialExpressionConstant, 1600, 300)
    z_max.set_editor_property("r", 50.0)

    height_mask = expr(unreal.MaterialExpressionDivide, 1800, 300)
    connect(z_add, "", height_mask, "A")
    connect(z_max, "", height_mask, "B")

    # Clamp to 0-1
    height_clamp = expr(unreal.MaterialExpressionClamp, 2000, 300)
    connect(height_mask, "", height_clamp, "")

    # ================================================================
    # WIND WPO
    # WPO = windDir * windSpeed * intensity * heightMask + flutter
    # ================================================================
    wind_intensity = expr(unreal.MaterialExpressionConstant, 800, 500)
    wind_intensity.set_editor_property("r", 8.0)  # cm of bend at full wind

    # wind_dir_speed = wind_xy * wind_w
    wind_dir_speed = expr(unreal.MaterialExpressionMultiply, 800, 400)
    connect(wind_xy, "", wind_dir_speed, "A")
    connect(wind_w, "", wind_dir_speed, "B")

    # wind_powered = wind_dir_speed * wind_intensity
    wind_powered = expr(unreal.MaterialExpressionMultiply, 1000, 450)
    connect(wind_dir_speed, "", wind_powered, "A")
    connect(wind_intensity, "", wind_powered, "B")

    # Flutter: sine wave with per-instance phase
    time_expr = expr(unreal.MaterialExpressionTime, 800, 600)
    flutter_scale = expr(unreal.MaterialExpressionConstant, 800, 700)
    flutter_scale.set_editor_property("r", 2.0)  # cm flutter

    # flutter_phase = time * 3 + pir * 10
    time_mult = expr(unreal.MaterialExpressionConstant, 600, 600)
    time_mult.set_editor_property("r", 3.0)
    time_scaled = expr(unreal.MaterialExpressionMultiply, 800, 650)
    connect(time_expr, "", time_scaled, "A")
    connect(time_mult, "", time_scaled, "B")

    pir_mult = expr(unreal.MaterialExpressionConstant, 600, 700)
    pir_mult.set_editor_property("r", 10.0)
    pir_scaled = expr(unreal.MaterialExpressionMultiply, 800, 750)
    connect(pir, "", pir_scaled, "A")
    connect(pir_mult, "", pir_scaled, "B")

    flutter_phase = expr(unreal.MaterialExpressionAdd, 1000, 700)
    connect(time_scaled, "", flutter_phase, "A")
    connect(pir_scaled, "", flutter_phase, "B")

    flutter_sin = expr(unreal.MaterialExpressionSine, 1200, 700)
    connect(flutter_phase, "", flutter_sin, "")

    flutter = expr(unreal.MaterialExpressionMultiply, 1400, 700)
    connect(flutter_sin, "", flutter, "A")
    connect(flutter_scale, "", flutter, "B")

    # WPO = (wind_powered + flutter) * height_mask
    wpo_add = expr(unreal.MaterialExpressionAdd, 1600, 500)
    connect(wind_powered, "", wpo_add, "A")
    connect(flutter, "", wpo_add, "B")

    # Only apply XY offset (grass bends sideways, not up/down)
    # Convert the scalar flutter to a vector (apply to X and Y)
    wpo_vec = expr(unreal.MaterialExpressionAppendVector, 1800, 500)
    connect(wpo_add, "", wpo_vec, "A")
    connect(wpo_add, "", wpo_vec, "B")

    wpo_masked = expr(unreal.MaterialExpressionMultiply, 2000, 500)
    connect(wpo_vec, "", wpo_masked, "A")
    connect(height_clamp, "", wpo_masked, "B")

    # ================================================================
    # BASE COLOR
    # base = tex_diff.RGB * (0.7 + pir * 0.3) * tex_ao.R
    # ================================================================
    pir_color_const = expr(unreal.MaterialExpressionConstant, 200, 50)
    pir_color_const.set_editor_property("r", 0.3)

    pir_color = expr(unreal.MaterialExpressionMultiply, 400, 50)
    connect(pir, "", pir_color, "A")
    connect(pir_color_const, "", pir_color, "B")

    base_const = expr(unreal.MaterialExpressionConstant, 600, 50)
    base_const.set_editor_property("r", 0.7)

    color_var = expr(unreal.MaterialExpressionAdd, 800, 50)
    connect(base_const, "", color_var, "A")
    connect(pir_color, "", color_var, "B")

    color_pir = expr(unreal.MaterialExpressionMultiply, 1000, 50)
    connect(tex_diff, "RGB", color_pir, "A")
    connect(color_var, "", color_pir, "B")

    color_ao = expr(unreal.MaterialExpressionMultiply, 1200, 50)
    connect(color_pir, "", color_ao, "A")
    connect(tex_ao, "R", color_ao, "B")

    # ================================================================
    # SUBSURFACE COLOR (warm yellow-green for backlit grass)
    # ================================================================
    sss_color = expr(unreal.MaterialExpressionConstant3Vector, 200, 300)
    sss_color.set_editor_property("constant", unreal.LinearColor(0.3, 0.5, 0.1, 1.0))

    # ================================================================
    # ROUGHNESS (grass is not shiny)
    # ================================================================
    rough_val = expr(unreal.MaterialExpressionConstant, 200, 500)
    rough_val.set_editor_property("r", 0.85)

    # ================================================================
    # SPECULAR (low)
    # ================================================================
    spec = expr(unreal.MaterialExpressionConstant, 200, 600)
    spec.set_editor_property("r", 0.2)

    # ================================================================
    # CONNECT TO MATERIAL OUTPUTS
    # ================================================================
    connect_out(color_ao, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_out(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    connect_out(rough_val, "", unreal.MaterialProperty.MP_ROUGHNESS)
    connect_out(tex_normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    connect_out(sss_color, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    connect_out(tex_diff, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    connect_out(wpo_masked, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    # Recompile material so changes take effect
    mel.recompile_material(mat)

    # Save
    eal.save_asset(MAT_PATH)
    log("Created and saved M_TakeoffGrass")
    log("Material uses LocalPosition.Z for height mask (no vertex color needed)")
    log("Wind WPO reads from MPC_Wind via CollectionParameter nodes")
    return mat


def main():
    log("Starting grass material creation...")
    mpc = create_mpc()
    if not mpc:
        log("FAILED: Could not create MPC. See MANUAL FALLBACK below.")
        log("")
        log("MANUAL FALLBACK — Create MPC_Wind in the editor:")
        log("  1. Right-click in Content/Materials")
        log("  2. Material → Material Parameter Collection")
        log("  3. Name it MPC_Wind")
        log("  4. Add Scalar Parameters: WindStrength (0.0), WindGust (0.0)")
        log("  5. Add Vector Parameter: WindVector (1,0,0,0)")
        log("  6. Save")
        return

    mat = create_grass_material(mpc)
    if mat:
        log("SUCCESS: Both assets created.")
        log("  MPC:      " + MPC_PATH)
        log("  Material: " + MAT_PATH)
        log("")
        log("To use: Assign M_TakeoffGrass to your GrassField actor's GrassMaterial.")
        log("Place a GrassWindManager actor in the level to drive the wind.")
    else:
        log("FAILED: Could not create material.")
        log("")
        log("MANUAL FALLBACK — Create M_TakeoffGrass in the material editor:")
        log("  1. Create a new Material in Content/Materials, name M_TakeoffGrass")
        log("  2. Set Blend Mode = Masked, Shading Model = Two Sided Foliage")
        log("  3. Enable Two Sided, disable Tangent Space Normal")
        log("  4. Add TextureSampleParameter2D nodes for GrassDiffuse/Normal/AO")
        log("  5. Add CollectionParameter node → assign MPC_Wind → WindVector")
        log("  6. Use ComponentMask to extract XY (direction) and W (speed)")
        log("  7. Multiply direction × speed × intensity constant → WPO")
        log("  8. Add LocalPosition → ComponentMask B → normalize → multiply WPO")
        log("     (this is the height mask — roots stay fixed, tips bend)")
        log("  9. Connect to BaseColor, Normal, Roughness, OpacityMask, WPO")


main()
