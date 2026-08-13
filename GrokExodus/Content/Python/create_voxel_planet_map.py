"""
Create Lvl_VoxelPlanet with VoxelGameMode + Sun lighting setup.
Run: UnrealEditor-Cmd GrokExodus.uproject -ExecutePythonScript=.../create_voxel_planet_map.py
"""

import unreal


LEVEL_PATH = "/Game/Voxel/Maps/Lvl_VoxelPlanet"
GAME_MODE = "/Script/GrokExodus.GXGameMode"


def log(msg: str) -> None:
    unreal.log(f"[VoxelPlanetMap] {msg}")


def set_component_props(actor, component_name: str, props: dict) -> None:
    """Set properties on a named component if present."""
    comps = actor.get_components_by_class(unreal.ActorComponent)
    target = None
    for c in comps:
        if c.get_name() == component_name or component_name in c.get_name():
            target = c
            break
    if target is None and comps:
        # Fall back to first matching class name substring
        for c in comps:
            if component_name.lower() in c.get_class().get_name().lower():
                target = c
                break
    if target is None:
        log(f"  component '{component_name}' not found on {actor.get_actor_label()}")
        return
    for key, value in props.items():
        try:
            target.set_editor_property(key, value)
        except Exception as e:
            log(f"  set {component_name}.{key} failed: {e}")


def main() -> None:
    log("Creating voxel planet level...")

    # Prefer LevelEditorSubsystem (UE5)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les is None:
        raise RuntimeError("LevelEditorSubsystem unavailable")

    # New empty level (template none)
    # UE 5.4+ API: new_level(asset_path, is_partitioned=False, world_partition_grid_size optional)
    try:
        les.new_level(LEVEL_PATH)
    except TypeError:
        les.new_level(LEVEL_PATH, False)

    log(f"Level created/open: {les.get_current_level()}")

    # --- World / Game Mode ---
    world = unreal.EditorLevelLibrary.get_editor_world()
    world_settings = world.get_world_settings()
    # GameMode override for this map only
    gm_class = unreal.load_class(None, GAME_MODE)
    if gm_class is None:
        # Alternate load path
        gm_class = unreal.EditorAssetLibrary.load_blueprint_class(GAME_MODE)
    if gm_class is not None:
        world_settings.set_editor_property("default_game_mode", gm_class)
        log(f"GameMode set to {GAME_MODE}")
    else:
        log(f"WARNING: could not load game mode class {GAME_MODE}")

    # Kill default gravity feeling for spherical planet (movement uses custom gravity)
    # Keep world gravity magnitude for CMC scale; direction is set per-frame.
    try:
        world_settings.set_editor_property("global_gravity_z", -980.0)
    except Exception:
        pass

    # --- Atmosphere / sky for outdoor planet ---
    # Sky Atmosphere (Earth-like scattering so the sun disc and sky work with directional light)
    sky_atmo = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)
    )
    if sky_atmo:
        sky_atmo.set_actor_label("SkyAtmosphere_Planet")
        log("Spawned SkyAtmosphere")

    # Exponential height fog — light Earth haze (planet surface scale)
    fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0)
    )
    if fog:
        fog.set_actor_label("HeightFog_Planet")
        set_component_props(
            fog,
            "ExponentialHeightFogComponent",
            {
                "fog_density": 0.008,
                "fog_height_falloff": 0.12,
                "fog_inscattering_color": unreal.LinearColor(0.45, 0.55, 0.75, 1.0),
                "directional_incattering_exponent": 8.0,
                "directional_incattering_start_distance": 2000.0,
                "directional_incattering_color": unreal.LinearColor(1.0, 0.92, 0.75, 1.0),
                "volumetric_fog": True,
                "volumetric_fog_scattering_distribution": 0.3,
                "volumetric_fog_extinction_scale": 0.6,
            },
        )
        log("Spawned ExponentialHeightFog")

    # Sky Light — ambient fill from atmosphere (real-time capture)
    sky_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 0)
    )
    if sky_light:
        sky_light.set_actor_label("SkyLight_Planet")
        set_component_props(
            sky_light,
            "SkyLightComponent",
            {
                "mobility": unreal.ComponentMobility.MOVABLE,
                "real_time_capture": True,
                "intensity": 1.0,
                "lower_hemisphere_is_black": False,
            },
        )
        log("Spawned SkyLight (real-time capture)")

    # --- THE SUN: Directional Light ---
    # Directional lights are at infinite distance; orientation = sun direction in the sky.
    # Pitch -45° / Yaw 35° gives a high afternoon sun for good surface relief.
    sun_rot = unreal.Rotator(-48.0, 35.0, 0.0)
    sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight,
        unreal.Vector(0, 0, 0),
        sun_rot,
    )
    if sun:
        sun.set_actor_label("Sun_DirectionalLight")
        # Physical-scale outdoor sun for UE5 Lumen / auto-exposure
        # Intensity: ~10 lux is UE default "bright day" when using lux units with atmosphere.
        # AtmosphereSunLight links this light as the atmosphere sun disc.
        set_component_props(
            sun,
            "DirectionalLightComponent",
            {
                "mobility": unreal.ComponentMobility.MOVABLE,
                "intensity": 12.0,  # lux-scale outdoor; pairs with AutoExposure
                "light_color": unreal.Color(255, 244, 214, 255),  # warm sunlight ~5500-6000K look
                "temperature": 5800.0,
                "use_temperature": True,
                "atmosphere_sun_light": True,
                "atmosphere_sun_light_index": 0,
                "cast_shadows": True,
                "cast_modulated_shadows": False,
                "shadow_amount": 1.0,
                "specular_scale": 1.0,
                "indirect_lighting_intensity": 1.0,
                "volumetric_scattering_intensity": 1.0,
                # Soft sun disc (degrees) — real sun ~0.53°; slightly larger for softer terrain shadows
                "light_source_angle": 0.535,
                "light_source_soft_angle": 0.0,
                # Cascades for large spherical terrain
                "dynamic_shadow_distance_movable_light": 40000.0,  # 400 m in cm
                "dynamic_shadow_cascades": 4,
                "cascade_distribution_exponent": 2.5,
                "cascade_transition_fraction": 0.12,
                "shadow_bias": 0.2,
                "shadow_slope_bias": 0.5,
                "samples_per_pixel": 1,
            },
        )
        log("Spawned Sun DirectionalLight (intensity=12, temp=5800K, atmosphere sun, 4 cascades)")

    # Optional: Post-process for outdoor exposure (planet surface albedo is mid-gray terrain)
    pp = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0, 0, 0)
    )
    if pp:
        pp.set_actor_label("PostProcess_PlanetOutdoor")
        try:
            pp.set_editor_property("unbound", True)  # affect whole world
        except Exception:
            try:
                pp.set_editor_property("b_unbound", True)
            except Exception:
                pass
        # Exposure: outdoor day range
        try:
            settings = pp.get_editor_property("settings")
            # Enable auto exposure overrides if API allows
            settings.set_editor_property("override_auto_exposure_method", True)
            settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
            settings.set_editor_property("override_auto_exposure_min_brightness", True)
            settings.set_editor_property("auto_exposure_min_brightness", 0.6)
            settings.set_editor_property("override_auto_exposure_max_brightness", True)
            settings.set_editor_property("auto_exposure_max_brightness", 2.0)
            settings.set_editor_property("override_auto_exposure_bias", True)
            settings.set_editor_property("auto_exposure_bias", 0.0)
            pp.set_editor_property("settings", settings)
        except Exception as e:
            log(f"PostProcess exposure tweak skipped: {e}")
        log("Spawned unbound PostProcessVolume")

    # Player start near +X surface (game mode also repositions; this helps PIE camera)
    # Planet radius default 512 m = 51200 cm
    surface_cm = 51200.0 + 5000.0
    ps = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(surface_cm, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if ps:
        ps.set_actor_label("PlayerStart_PlanetSurface")
        log(f"Spawned PlayerStart at +X ~{surface_cm} cm")

    # Save level
    try:
        les.save_current_level()
    except Exception:
        unreal.EditorLevelLibrary.save_current_level()

    log(f"Saved level {LEVEL_PATH}")
    log("Done.")


if __name__ == "__main__":
    main()
