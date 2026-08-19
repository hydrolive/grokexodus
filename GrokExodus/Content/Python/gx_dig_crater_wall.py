"""Dig a crater then punch the wall. Run in PIE via MCP execute_python."""
import unreal


def _pie_world():
    try:
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if worlds:
            return worlds[0]
    except Exception:
        pass
    try:
        return unreal.EditorLevelLibrary.get_game_world()
    except Exception:
        return None


def _actors(world, cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, cls))


def main():
    world = _pie_world()
    if not world:
        unreal.log_error("[GXDIG] no PIE world")
        return
    worlds = _actors(world, unreal.GXVoxelWorld)
    pawns = _actors(world, unreal.Pawn)
    if not worlds:
        unreal.log_error("[GXDIG] no GXVoxelWorld")
        return
    vw = worlds[0]
    pawn = None
    for p in pawns:
        if "Survivor" in p.get_name() or "Character" in p.get_class().get_name():
            pawn = p
            break
    if not pawn and pawns:
        pawn = pawns[0]
    if not pawn:
        unreal.log_error("[GXDIG] no pawn")
        return
    loc = pawn.get_actor_location()
    grav = vw.get_gravity_direction_at(loc)
    up = grav * -1.0
    up = up / up.length()
    hit = vw.raycast_voxels(loc + up * 80.0, grav, 2500.0)
    hit_ok = bool(getattr(hit, "hit", getattr(hit, "b_hit", False)))
    if not hit_ok:
        unreal.log_error("[GXDIG] no surface hit attrs=%s" % [a for a in dir(hit) if not a.startswith("_")])
        return
    surf = hit.location
    nrm = hit.normal
    if nrm.length() < 0.1:
        nrm = up
    else:
        nrm = nrm / nrm.length()
    fwd = pawn.get_actor_forward_vector()
    if fwd.length() < 0.1:
        fwd = unreal.Vector(0, 1, 0)
    else:
        fwd = fwd / fwd.length()
    unreal.log("[GXDIG] surf=%s n=%s" % (surf, nrm))

    r = 1.2
    crater = surf - nrm * (r * 0.18 * 100.0)
    for i in range(8):
        center = crater - nrm * (i * 0.55 * 100.0)
        vw.dig_sphere(center, r, 1.0, 1.0, 1.0)
    # Wall strokes: from inside the pit, push into the forward dirt.
    wall_base = crater - nrm * (3.2 * 100.0) + fwd * (1.6 * 100.0)
    for i in range(6):
        center = wall_base + fwd * (i * 0.45 * 100.0)
        vw.dig_sphere(center, r, 1.0, 1.0, 1.0)
    pawn.set_actor_location(crater + nrm * 220.0 - fwd * 180.0, False, False)
    rot = unreal.MathLibrary.make_rot_from_xz(fwd + nrm * -0.90, nrm)
    pawn.set_actor_rotation(rot, False)
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if pc:
        pc.set_control_rotation(rot)
    unreal.log("[GXDIG] done crater+wall")


if __name__ == "__main__":
    main()
