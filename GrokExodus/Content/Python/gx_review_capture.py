# Capture look-around shots after the planet is Ready.
# Writes to Saved/Review/ (gitignored). Run from Unreal MCP, not by hand.

import os
import time
import unreal


def _review_dir():
    d = os.path.join(unreal.Paths.project_saved_dir(), "Review")
    os.makedirs(d, exist_ok=True)
    return d


def capture(tag="look"):
    dest = _review_dir()
    name = "GX-review-%s" % tag
    path = os.path.join(dest, name + ".png")
    try:
        unreal.AutomationLibrary.take_high_res_screenshot(1920, 1080, path)
    except Exception:
        unreal.SystemLibrary.execute_console_command(
            unreal.EditorLevelLibrary.get_editor_world(),
            "HighResShot 1920x1080 filename=\"%s\"" % path.replace("\\", "/"))
    unreal.log("[GXReview] wrote %s" % path)
    return path


def look_and_capture():
    """Yaw the player view and shoot. Call once PIE is Ready."""
    world = unreal.EditorLevelLibrary.get_game_world()
    if world is None:
        world = unreal.EditorLevelLibrary.get_editor_world()
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    shots = []
    shots.append(capture("fwd"))
    if pawn:
        loc = pawn.get_actor_location()
        rot = pawn.get_actor_rotation()
        for i, yaw in enumerate((45.0, 90.0, 180.0, 270.0, -20.0)):
            r = unreal.Rotator(rot.pitch, rot.yaw + yaw, 0.0)
            if pc:
                pc.set_control_rotation(r)
            time.sleep(0.05)
            shots.append(capture("y%d" % int(yaw)))
        # look up at the horizon
        if pc:
            pc.set_control_rotation(unreal.Rotator(18.0, rot.yaw + 25.0, 0.0))
        shots.append(capture("horizon"))
    unreal.log("[GXReview] %d shots in %s" % (len(shots), _review_dir()))
    return shots


if __name__ == "__main__":
    look_and_capture()
