"""Build a hardware-safe GTA vehicle Extended Banner source scene.

The final animation is one rigid mesh-node TRS member.  No armature, skin,
bone index, or bone weight is introduced at any stage.
"""

from __future__ import annotations

import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector


FPS = 30
FRAME_START = 1
# Match the exact 36-frame / 72-HOME-tick envelope of the hardware-proven
# ClouDS banner while isolating the remaining playback problem.
OPEN_END = 5
OPEN_HOLD_END = 20
CLOSE_END = 24
LOOP_END = 36
OPEN_ANGLE_DEGREES = -90.0
WHEEL_ROTATIONS_PER_LOOP = 10.0
MOTION_FRAME_START = 0
# CTGP-7's proven vehicle banner runs for 360 HOME ticks.  Blender exports at
# 30 FPS while CGFX animation time is 60 Hz, so 180 authored frames produce
# the same 360-tick envelope.
MOTION_LOOP_END = 180
WHEEL_ROTATIONS_PER_MOTION_LOOP = 12.0
WORLD_SCALE = 2.65
WORLD_Z = 1.0
DISPLAY_YAW_DEGREES = -25.0
RIGID_ANIMATION_NODES = (
    "body_bob",
    "wheel_front",
    "wheel_rear",
)

ROAD_BOB_BASE = 0.030
ROAD_BOB_OFFSETS = (
    0.000, 0.000, 0.001, 0.006, 0.021, 0.010, -0.008, -0.003, 0.000,
    0.000, 0.000, 0.000, 0.003, 0.012, 0.005, -0.004, -0.001, 0.000,
    0.001, 0.009, 0.029, 0.015, -0.014, -0.007, -0.001, 0.000, 0.000,
    0.000, 0.000, 0.002, 0.008, 0.017, 0.006, -0.006, -0.002, 0.000,
)
ROAD_ROLL_DEGREES = (
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 1.4, 3.5, -3.0, -0.8, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
)
SHADOW_EXPANSION_RATIO = 0.10


def sample_loop_profile(
    profile: tuple[float, ...], progress: float, *, smooth: bool = True
) -> float:
    sample = (progress % 1.0) * len(profile)
    index = int(math.floor(sample)) % len(profile)
    fraction = sample - math.floor(sample)
    # Smooth each segment without turning the deliberately uneven event
    # spacing and amplitudes back into a regular sinusoid.
    blend = (
        fraction * fraction * (3.0 - 2.0 * fraction)
        if smooth
        else fraction
    )
    current = profile[index]
    following = profile[(index + 1) % len(profile)]
    return current + (following - current) * blend


def road_bob_height(progress: float) -> float:
    return ROAD_BOB_BASE + sample_loop_profile(ROAD_BOB_OFFSETS, progress)


def road_roll_angle(progress: float) -> float:
    return math.radians(
        sample_loop_profile(ROAD_ROLL_DEGREES, progress, smooth=False)
    )


@dataclass(frozen=True)
class VehicleSpec:
    game: str
    model_name: str
    dff_name: str
    txd_name: str
    wheel_txd_name: str
    wheel_node: str
    wheel_scale: float
    flip_left_wheels: bool
    primary_rgb: tuple[int, int, int]
    secondary_rgb: tuple[int, int, int]


SPECS = {
    "gta3": VehicleSpec(
        game="gta3",
        model_name="kuruma",
        dff_name="kuruma.dff",
        txd_name="KURUMA.TXD",
        wheel_txd_name="MISC.TXD",
        wheel_node="wheel_saloon",
        wheel_scale=0.70,
        flip_left_wheels=False,
        primary_rgb=(75, 125, 130),  # Give Me Liberty: colour 58
        secondary_rgb=(245, 245, 245),  # Give Me Liberty: colour 1
    ),
    "vc": VehicleSpec(
        game="vc",
        model_name="admiral",
        dff_name="admiral.dff",
        txd_name="admiral.txd",
        wheel_txd_name="wheels.txd",
        wheel_node="wheel_alloy",
        wheel_scale=0.68,
        flip_left_wheels=True,
        primary_rgb=(224, 223, 214),  # In The Beginning: colour 84
        secondary_rgb=(224, 223, 214),
    ),
    "lcs": VehicleSpec(
        game="lcs",
        model_name="mafia",
        dff_name="MAFIA.dff",
        txd_name="mafia.txd",
        # LCS keeps its higher-detail shared wheel textures in generic.txd.
        wheel_txd_name="generic.txd",
        wheel_node="wheel_alloy",
        wheel_scale=0.70,
        flip_left_wheels=True,
        primary_rgb=(15, 15, 15),  # Leone Sentinel: carcols.dat colour 0
        secondary_rgb=(15, 15, 15),
    ),
}


def arguments() -> tuple[Path, Path, str]:
    separator = sys.argv.index("--")
    args = sys.argv[separator + 1 :]
    if len(args) != 3 or args[2] not in SPECS:
        raise SystemExit(
            "expected DRAGONFF_ROOT OUTPUT_DIR "
            + "|".join(SPECS)
            + " after --"
        )
    return Path(args[0]).resolve(), Path(args[1]).resolve(), args[2]


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in tuple(bpy.data.collections):
        if collection != bpy.context.scene.collection:
            bpy.data.collections.remove(collection)


def import_txd_casefold(import_txd, path: Path) -> dict:
    imported = import_txd(
        {"file_name": str(path), "skip_mipmaps": True, "pack": True}
    )
    result = {}
    for name, images in imported.images.items():
        for level, image in enumerate(images):
            safe_name = "_".join(
                part for part in (path.stem, name, str(level)) if part
            ).replace("/", "_").replace("\\", "_")
            image.name = safe_name
        result[name] = images
        result[name.casefold()] = images
        result[name.upper()] = images
    return result


def import_dff_scene(import_dff, path: Path, txd_images: dict) -> None:
    import_dff(
        {
            "file_name": str(path),
            "txd_images": txd_images,
            "image_ext": "",
            "connect_bones": False,
            "use_mat_split": False,
            "remove_doubles": False,
            "create_backfaces": False,
            "group_materials": False,
            "import_normals": True,
            "materials_naming": "DEFAULT",
            "import_breakable": False,
            "hide_damage_parts": False,
        }
    )


def remove_damage_and_low_detail() -> None:
    for obj in tuple(bpy.context.scene.objects):
        lower = obj.name.casefold()
        if obj.type == "MESH" and (lower.endswith("_dam") or lower.endswith("_vlo")):
            bpy.data.objects.remove(obj, do_unlink=True)


def add_wheels(import_dff, source_dir: Path, txd_images: dict, spec: VehicleSpec) -> None:
    before = set(bpy.context.scene.objects)
    import_dff_scene(import_dff, source_dir / "wheels.dff", txd_images)
    imported = set(bpy.context.scene.objects) - before
    wheel_mesh = bpy.data.objects.get(spec.wheel_node + "_l0")
    if wheel_mesh is None or wheel_mesh.type != "MESH" or wheel_mesh not in imported:
        raise RuntimeError(f"missing wheel mesh {spec.wheel_node}_l0")

    for dummy_name in ("wheel_lf_dummy", "wheel_rf_dummy", "wheel_lb_dummy", "wheel_rb_dummy"):
        dummy = bpy.data.objects.get(dummy_name)
        if dummy is None:
            raise RuntimeError(f"missing vehicle node {dummy_name}")
        wheel = wheel_mesh.copy()
        wheel.data = wheel_mesh.data
        wheel.name = dummy_name.replace("_dummy", "")
        bpy.context.scene.collection.objects.link(wheel)
        wheel.parent = dummy
        wheel.location = (0.0, 0.0, 0.0)
        wheel.rotation_mode = "XYZ"
        wheel.rotation_euler = (
            0.0,
            0.0,
            math.pi
            if spec.flip_left_wheels and dummy_name in {"wheel_lf_dummy", "wheel_lb_dummy"}
            else 0.0,
        )
        wheel.scale = (spec.wheel_scale,) * 3

    for obj in imported:
        bpy.data.objects.remove(obj, do_unlink=True)


def reparent_driver_door() -> bpy.types.Object:
    door = bpy.data.objects.get("door_lf_hi_ok")
    dummy = bpy.data.objects.get("door_lf_dummy")
    chassis = bpy.data.objects.get("chassis_dummy")
    if door is None or dummy is None or chassis is None:
        raise RuntimeError("vehicle is missing the left-front door hierarchy")
    if dummy.parent != chassis or door.parent != dummy:
        raise RuntimeError("unexpected left-front door parent chain")
    # Compose the original DFF node matrices directly.  Reading matrix_world
    # immediately after DragonFF import can return a stale identity matrix
    # until Blender's dependency graph updates, which incorrectly moves the
    # hinge to the vehicle origin.
    closed_local = dummy.matrix_basis.copy() @ door.matrix_basis.copy()
    door.parent = chassis
    door.matrix_local = closed_local
    door.name = "driver_door"
    return door


def prune_leaf_empties(protected: set[str]) -> None:
    changed = True
    while changed:
        changed = False
        for obj in tuple(bpy.context.scene.objects):
            if obj.type != "EMPTY" or obj.name in protected or obj.children:
                continue
            bpy.data.objects.remove(obj, do_unlink=True)
            changed = True


def set_principled(material: bpy.types.Material, rgba: tuple[float, float, float, float]) -> None:
    material.diffuse_color = rgba
    from bpy_extras.node_shader_utils import PrincipledBSDFWrapper

    wrapper = PrincipledBSDFWrapper(material, is_readonly=False)
    wrapper.base_color = rgba[:3]
    wrapper.roughness = 0.72
    wrapper.specular = 0.0
    principled = wrapper.node_principled_bsdf
    principled.inputs["Base Color"].default_value[3] = rgba[3]
    principled.inputs["Alpha"].default_value = rgba[3]
    base_color = principled.inputs["Base Color"]
    if base_color.is_linked:
        source_link = base_color.links[0]
        source_socket = source_link.from_socket
        nodes = material.node_tree.nodes
        links = material.node_tree.links
        links.remove(source_link)
        multiply = nodes.new("ShaderNodeMixRGB")
        multiply.name = "GTA Vehicle Colour Multiply"
        multiply.label = "Original mission vehicle colour"
        multiply.blend_type = "MULTIPLY"
        multiply.inputs[0].default_value = 1.0
        multiply.inputs[2].default_value = rgba
        links.new(source_socket, multiply.inputs[1])
        links.new(multiply.outputs[0], base_color)


def apply_mission_colours(spec: VehicleSpec) -> None:
    primary = tuple(channel / 255.0 for channel in spec.primary_rgb) + (1.0,)
    secondary = tuple(channel / 255.0 for channel in spec.secondary_rgb) + (1.0,)
    for material in bpy.data.materials:
        lower = material.name.casefold()
        texture_names: set[str] = set()
        if material.use_nodes and material.node_tree:
            texture_names = {
                node.image.name.casefold()
                for node in material.node_tree.nodes
                if node.type == "TEX_IMAGE" and node.image is not None
            }
        # LCS' PS2 vehicle materials do not carry the PC-era primary/secondary
        # names used by re3 and reVC.  Their paint masks are the dedicated
        # xv_vehiclegrunge/xv_bodypanels textures; colour those explicitly so
        # the Leone Sentinel follows carcols.dat without tinting glass, lamps,
        # tyres, or the detailed alloy-wheel materials.
        lcs_primary = spec.game == "lcs" and any(
            "xv_vehiclegrunge" in name or "xv_bodypanels" in name
            for name in texture_names
        )
        if lower == "primary" or lower.startswith("primary.") or lcs_primary:
            set_principled(material, primary)
        elif lower == "secondary" or lower.startswith("secondary."):
            set_principled(material, secondary)
        elif lower == "glass" or lower.startswith("glass."):
            rgba = tuple(material.diffuse_color)
            set_principled(material, rgba)
            material.use_backface_culling = False
            if hasattr(material, "surface_render_method"):
                material.surface_render_method = "DITHERED"


def material_signature(material: bpy.types.Material) -> tuple:
    images = ()
    if material.use_nodes and material.node_tree:
        images = tuple(
            node.image.name.casefold()
            for node in material.node_tree.nodes
            if node.type == "TEX_IMAGE" and node.image is not None
        )
    return tuple(round(value, 6) for value in material.diffuse_color) + images


def consolidate_materials() -> None:
    canonical: dict[tuple, bpy.types.Material] = {}
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        for slot in obj.material_slots:
            material = slot.material
            if material is None:
                continue
            signature = material_signature(material)
            if signature not in canonical:
                canonical[signature] = material
            else:
                slot.material = canonical[signature]


def create_roots(vehicle: bpy.types.Object) -> tuple[bpy.types.Object, bpy.types.Object]:
    common = bpy.data.objects.new("COMMON", None)
    world = bpy.data.objects.new("world", None)
    bpy.context.scene.collection.objects.link(common)
    bpy.context.scene.collection.objects.link(world)
    world.parent = common
    vehicle.parent = world
    vehicle.name = "vehicle"
    world.scale = (WORLD_SCALE,) * 3
    world.location.z = WORLD_Z
    return common, world


def attach_driver_door_to_world(
    door: bpy.types.Object, world: bpy.types.Object
) -> None:
    """Make the animated rigid mesh a direct child of HOME's world node.

    HOME Menu banner skeletal animation is reliable for a rigid mesh directly
    below ``world``.  Keeping the door buried in the imported DFF chassis
    hierarchy makes the animation member resolve to the rotating vehicle
    ancestor on hardware.  Preserve the hinge's world transform while moving
    only this mesh out of that hierarchy.
    """
    bpy.context.view_layer.update()


def flatten_vehicle_for_home(
    world: bpy.types.Object, door: bpy.types.Object
) -> None:
    """Reduce the imported DFF tree to the proven ClouDS rigid layout.

    HOME's banner animation path is reliable with a handful of direct rigid
    children below ``world``.  Exporting every GTA dummy created a 37-bone
    skeleton and hardware resolved the door CANM rotation to the vehicle root.
    Bake the static hierarchy into one mesh node, retain the door as the only
    independently animated rigid node, and centre both around the four wheel
    contact points before applying the authored three-quarter yaw.
    """
    bpy.context.view_layer.update()
    to_world = world.matrix_world.inverted()
    wheel_objects = [
        bpy.data.objects[name]
        for name in ("wheel_lf", "wheel_rf", "wheel_lb", "wheel_rb")
    ]
    wheel_centres = [to_world @ obj.matrix_world.translation for obj in wheel_objects]
    centre = Vector(
        (
            sum(point.x for point in wheel_centres) / len(wheel_centres),
            sum(point.y for point in wheel_centres) / len(wheel_centres),
            0.0,
        )
    )
    authored = (
        Matrix.Rotation(math.radians(DISPLAY_YAW_DEGREES), 4, "Z")
        @ Matrix.Translation(-centre)
    )

    shadow = bpy.data.objects.get("native_car_shadow")
    if shadow is None:
        raise RuntimeError("native car shadow must exist before flattening")
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    static_meshes = [obj for obj in meshes if obj not in {door, shadow}]
    if not static_meshes:
        raise RuntimeError("vehicle has no static meshes to flatten")

    # Every wheel clone initially shares mesh data.  Copy before applying each
    # object's distinct transform, then collapse all static parts to one rigid
    # node just like the hardware-proven ClouDS banner.
    for obj in (*static_meshes, shadow):
        local = authored @ to_world @ obj.matrix_world
        obj.data = obj.data.copy()
        obj.data.transform(local)
        obj.parent = world
        obj.matrix_local = Matrix.Identity(4)

    # Match ClouDS' proven animated-mesh contract exactly: the rigid mesh node
    # is identity at rest.  Bake the complete closed pose into its vertices and
    # keep the hinge only as build metadata; the animation supplies the local
    # translation needed to rotate those baked vertices about the real hinge.
    # The ClouDS-style three-member CANM dictionary prevents this transform
    # from being misresolved to the banner root on HOME Menu.
    door_local = authored @ to_world @ door.matrix_world
    door.data = door.data.copy()
    hinge = door_local.translation
    door.data.transform(door_local)
    door.parent = world
    door.matrix_local = Matrix.Identity(4)
    door.rotation_mode = "XYZ"
    door["banner_hinge_x"] = hinge.x
    door["banner_hinge_y"] = hinge.y
    door["banner_hinge_z"] = hinge.z

    bpy.ops.object.select_all(action="DESELECT")
    for obj in static_meshes:
        # DragonFF marks detachable body panels hidden in the viewport even
        # though glTF still exports them.  Blender's join operator silently
        # ignores hidden selections, so expose them only for this build step.
        obj.hide_set(False)
        obj.hide_viewport = False
        obj.hide_render = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = static_meshes[0]
    bpy.ops.object.join()
    static_meshes[0].name = "vehicle_body"
    bpy.context.view_layer.update()
    closed_world = door.matrix_world.copy()
    door.parent = world
    door.matrix_world = closed_world
    bpy.context.view_layer.update()


def flatten_vehicle_for_high_speed_motion(
    world: bpy.types.Object,
) -> tuple[bpy.types.Object, bpy.types.Object, bpy.types.Object]:
    """Build the CTGP-7 body-parent/two-local-axle banner hierarchy."""

    bpy.context.view_layer.update()
    to_world = world.matrix_world.inverted()
    wheel_objects = {
        name: bpy.data.objects[name]
        for name in ("wheel_lf", "wheel_rf", "wheel_lb", "wheel_rb")
    }
    wheel_centres = {
        name: to_world @ obj.matrix_world.translation
        for name, obj in wheel_objects.items()
    }
    centre = Vector(
        (
            sum(point.x for point in wheel_centres.values()) / 4.0,
            sum(point.y for point in wheel_centres.values()) / 4.0,
            0.0,
        )
    )
    centred = Matrix.Translation(-centre)
    authored = Matrix.Rotation(math.radians(DISPLAY_YAW_DEGREES), 4, "Z") @ centred

    shadow = bpy.data.objects.get("native_car_shadow")
    if shadow is None:
        raise RuntimeError("native car shadow must exist before flattening")
    all_meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    wheels = set(wheel_objects.values())
    static_meshes = [
        obj for obj in all_meshes if obj not in wheels and obj != shadow
    ]
    if not static_meshes:
        raise RuntimeError("vehicle has no static body meshes")

    # CTGP-7 keeps the vehicle yaw on the animated body bone, places each
    # wheel bone at its real axle centre, and animates only the wheel's local X
    # rotation.  Do not bake the yaw into body/wheel vertices: doing that
    # forces a multi-axis rotation plus pivot-compensation translation, which
    # HOME's banner player either ignores or resolves against the wrong node.
    for obj in (*static_meshes, *wheel_objects.values()):
        local = centred @ to_world @ obj.matrix_world
        obj.data = obj.data.copy()
        obj.data.transform(local)
        obj.parent = world
        obj.matrix_local = Matrix.Identity(4)
        obj.hide_set(False)
        obj.hide_viewport = False
        obj.hide_render = False

    shadow_local = authored @ to_world @ shadow.matrix_world
    shadow.data = shadow.data.copy()
    shadow.data.transform(shadow_local)
    shadow.parent = world
    shadow.matrix_local = Matrix.Identity(4)
    shadow.hide_set(False)
    shadow.hide_viewport = False
    shadow.hide_render = False

    def join_meshes(objects: list[bpy.types.Object], name: str) -> bpy.types.Object:
        bpy.ops.object.select_all(action="DESELECT")
        for obj in objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        bpy.ops.object.join()
        objects[0].name = name
        return objects[0]

    body = join_meshes(static_meshes, "body_bob")
    front = join_meshes(
        [wheel_objects["wheel_lf"], wheel_objects["wheel_rf"]],
        "wheel_front",
    )
    rear = join_meshes(
        [wheel_objects["wheel_lb"], wheel_objects["wheel_rb"]],
        "wheel_rear",
    )

    motion_root = bpy.data.objects.new("vehicle_motion", None)
    bpy.context.scene.collection.objects.link(motion_root)
    motion_root.parent = world
    motion_root.location = (0.0, 0.0, 0.0)
    motion_root.rotation_mode = "XYZ"
    motion_root.rotation_euler = (
        0.0,
        0.0,
        math.radians(DISPLAY_YAW_DEGREES),
    )
    motion_root.scale = (1.0, 1.0, 1.0)

    for axle, pair in (
        (front, ("wheel_lf", "wheel_rf")),
        (rear, ("wheel_lb", "wheel_rb")),
    ):
        pivot = sum(
            (centred @ wheel_centres[name] for name in pair),
            Vector((0.0, 0.0, 0.0)),
        ) / 2.0
        axle.data.transform(Matrix.Translation(-pivot))
        axle.parent = motion_root
        axle.location = pivot
        axle.rotation_mode = "XYZ"
        axle.rotation_euler = (0.0, 0.0, 0.0)
        axle.scale = (1.0, 1.0, 1.0)

    body.parent = motion_root
    body.location = (0.0, 0.0, 0.0)
    body.rotation_mode = "XYZ"
    body.rotation_euler = (0.0, 0.0, 0.0)
    body.scale = (1.0, 1.0, 1.0)

    bpy.context.view_layer.update()
    return body, front, rear


def flatten_vehicle_static(world: bpy.types.Object) -> None:
    """Bake the complete car, closed doors and wheels into one static body."""

    bpy.context.view_layer.update()
    to_world = world.matrix_world.inverted()
    wheel_objects = [
        bpy.data.objects[name]
        for name in ("wheel_lf", "wheel_rf", "wheel_lb", "wheel_rb")
    ]
    wheel_centres = [to_world @ obj.matrix_world.translation for obj in wheel_objects]
    centre = Vector(
        (
            sum(point.x for point in wheel_centres) / 4.0,
            sum(point.y for point in wheel_centres) / 4.0,
            0.0,
        )
    )
    authored = (
        Matrix.Rotation(math.radians(DISPLAY_YAW_DEGREES), 4, "Z")
        @ Matrix.Translation(-centre)
    )
    shadow = bpy.data.objects.get("native_car_shadow")
    if shadow is None:
        raise RuntimeError("native car shadow must exist before flattening")
    meshes = [
        obj
        for obj in bpy.context.scene.objects
        if obj.type == "MESH" and obj != shadow
    ]
    if not meshes:
        raise RuntimeError("vehicle has no meshes to flatten")
    for obj in (*meshes, shadow):
        local = authored @ to_world @ obj.matrix_world
        obj.data = obj.data.copy()
        obj.data.transform(local)
        obj.parent = world
        obj.matrix_local = Matrix.Identity(4)
        obj.hide_set(False)
        obj.hide_viewport = False
        obj.hide_render = False
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    meshes[0].name = "vehicle_body"
    bpy.context.view_layer.update()


def animate_high_speed_motion(
    body: bpy.types.Object,
    front: bpy.types.Object,
    rear: bpy.types.Object,
) -> None:
    """Mirror CTGP-7: body vertical motion plus local-X axle rotation."""

    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    scene.frame_start = MOTION_FRAME_START
    scene.frame_end = MOTION_LOOP_END
    for frame in range(MOTION_FRAME_START, MOTION_LOOP_END + 1):
        progress = (frame - MOTION_FRAME_START) / (
            MOTION_LOOP_END - MOTION_FRAME_START
        )
        body.location = (0.0, 0.0, road_bob_height(progress))
        body.keyframe_insert("location", frame=frame)
        # Vehicle length is Blender Y; roll the shell around that axis while
        # the sibling wheel bones stay fixed on the road.
        body.rotation_mode = "XYZ"
        body.rotation_euler = (0.0, road_roll_angle(progress), 0.0)
        body.keyframe_insert("rotation_euler", frame=frame)

        wheel_angle = math.tau * WHEEL_ROTATIONS_PER_MOTION_LOOP * (
            0.5 - progress
        )
        for axle in (front, rear):
            axle.rotation_mode = "XYZ"
            axle.rotation_euler = (wheel_angle, 0.0, 0.0)
            axle.keyframe_insert("rotation_euler", frame=frame)

    for obj in (body, front, rear):
        if obj.animation_data and obj.animation_data.action:
            obj.animation_data.action.name = f"COMMON_{obj.name}"
            for fcurve in getattr(obj.animation_data.action, "fcurves", ()):
                for keyframe in fcurve.keyframe_points:
                    keyframe.interpolation = "LINEAR"
    scene.frame_set(MOTION_FRAME_START)


def scene_mesh_bounds(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (
        Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points))),
        Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points))),
    )


def create_shadow(world: bpy.types.Object, shadow_image: bpy.types.Image) -> bpy.types.Object:
    # The mesh objects are already children of a scaled `world` node.  Build
    # the plane in that node's local space; using world-space bounds and then
    # parenting the result to `world` scales and lifts the shadow twice.
    bpy.context.view_layer.update()
    to_local = world.matrix_world.inverted()
    wheel_objects = [
        bpy.data.objects[name]
        for name in ("wheel_lf", "wheel_rf", "wheel_lb", "wheel_rb")
    ]
    wheel_centres = [
        to_local @ wheel.matrix_world.translation for wheel in wheel_objects
    ]
    wheel_points = [
        to_local @ obj.matrix_world @ Vector(corner)
        for obj in wheel_objects
        for corner in obj.bound_box
    ]
    left = min(point.x for point in wheel_centres)
    right = max(point.x for point in wheel_centres)
    rear = min(point.y for point in wheel_centres)
    front = max(point.y for point in wheel_centres)
    horizontal_padding = (right - left) * SHADOW_EXPANSION_RATIO
    longitudinal_padding = (front - rear) * SHADOW_EXPANSION_RATIO
    left -= horizontal_padding
    right += horizontal_padding
    rear -= longitudinal_padding
    front += longitudinal_padding
    # Sit just below the tyre contact point.  The native texture supplies the
    # soft square shadow; the plane itself must never slice through a wheel.
    ground = min(point.z for point in wheel_points) - 0.015
    vertices = [
        (left, rear, ground),
        (right, rear, ground),
        (right, front, ground),
        (left, front, ground),
    ]
    mesh = bpy.data.meshes.new("native_car_shadow_mesh")
    mesh.from_pydata(vertices, [], [(0, 1, 2, 3)])
    uv = mesh.uv_layers.new(name="COMMON_SHADOW")
    for loop, coords in zip(uv.data, ((0, 0), (1, 0), (1, 1), (0, 1))):
        loop.uv = coords
    shadow = bpy.data.objects.new("native_car_shadow", mesh)
    bpy.context.scene.collection.objects.link(shadow)
    shadow.parent = world

    material = bpy.data.materials.new("native_shad_car")
    material.use_nodes = True
    material.diffuse_color = (1.0, 1.0, 1.0, 0.70)
    material.use_backface_culling = False
    if hasattr(material, "surface_render_method"):
        material.surface_render_method = "DITHERED"
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    principled = next(
        (node for node in nodes if node.type == "BSDF_PRINCIPLED"), None
    )
    if principled is None:
        principled = nodes.new("ShaderNodeBsdfPrincipled")
    output = next(
        (node for node in nodes if node.type == "OUTPUT_MATERIAL"), None
    )
    if output is None:
        output = nodes.new("ShaderNodeOutputMaterial")
    if not output.inputs["Surface"].is_linked:
        links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = shadow_image
    texture.interpolation = "Linear"
    links.new(texture.outputs["Color"], principled.inputs["Base Color"])
    links.new(texture.outputs["Alpha"], principled.inputs["Alpha"])
    principled.inputs["Roughness"].default_value = 1.0
    mesh.materials.append(material)
    return shadow


def animate_door(door: bpy.types.Object) -> None:
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    scene.frame_start = FRAME_START
    scene.frame_end = LOOP_END
    door.rotation_mode = "XYZ"
    hinge = Vector(
        (
            door["banner_hinge_x"],
            door["banner_hinge_y"],
            door["banner_hinge_z"],
        )
    )
    closed_angle = 0.0
    opened_angle = math.radians(OPEN_ANGLE_DEGREES)
    def angle_at(frame: int) -> float:
        if frame <= OPEN_END:
            return opened_angle * (frame - FRAME_START) / (
                OPEN_END - FRAME_START
            )
        if frame <= OPEN_HOLD_END:
            return opened_angle
        if frame <= CLOSE_END:
            return opened_angle * (1.0 - (frame - OPEN_HOLD_END) / (
                CLOSE_END - OPEN_HOLD_END
            ))
        return closed_angle

    # ClouDS' accepted CANM samples every TRS curve at every authored frame.
    # A microscopic cyclic scale variation prevents Blender from collapsing
    # this otherwise constant track to two keys; it is far below one pixel.
    for frame in range(FRAME_START, LOOP_END + 1):
        angle = angle_at(frame)
        phase = math.tau * (frame - FRAME_START) / (
            LOOP_END - FRAME_START
        )
        rotation = Matrix.Rotation(angle, 4, "Z")
        door.location = hinge - (rotation @ hinge)
        door.rotation_euler = (0.0, 0.0, angle)
        door.scale = (
            1.0 + math.sin(phase) * 1e-6,
            1.0 + math.cos(phase) * 1e-6,
            1.0 + math.sin(phase * 2.0) * 1e-6,
        )
        door.keyframe_insert("location", frame=frame)
        door.keyframe_insert("rotation_euler", frame=frame)
        door.keyframe_insert("scale", frame=frame)
    if door.animation_data and door.animation_data.action:
        door.animation_data.action.name = "COMMON"
        for fcurve in getattr(door.animation_data.action, "fcurves", ()):
            for keyframe in fcurve.keyframe_points:
                keyframe.interpolation = "LINEAR"
    for key in ("banner_hinge_x", "banner_hinge_y", "banner_hinge_z"):
        del door[key]
    scene.frame_set(FRAME_START)


def animate_identity_members() -> None:
    """Mirror ClouDS' three-member rigid CANM dictionary.

    A single-member CANM is accepted by the converter but HOME Menu resolves
    its lone transform against the banner root on hardware.  ClouDS uses three
    sibling rigid mesh members.  Give the two static GTA siblings explicit
    identity actions so the dictionary has the same proven shape while only
    the door contains changing samples.
    """
    scene = bpy.context.scene
    for name in ("native_car_shadow", "vehicle_body"):
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != "MESH":
            raise RuntimeError(f"missing rigid animation sibling {name}")
        obj.rotation_mode = "XYZ"
        for frame in range(FRAME_START, LOOP_END + 1):
            phase = math.tau * (frame - FRAME_START) / (
                LOOP_END - FRAME_START
            )
            obj.location = (
                math.sin(phase) * 1e-5,
                math.cos(phase) * 1e-5,
                math.sin(phase * 2.0) * 1e-5,
            )
            obj.rotation_euler = (
                math.sin(phase) * 1e-6,
                math.cos(phase) * 1e-6,
                math.sin(phase * 2.0) * 1e-6,
            )
            obj.scale = (
                1.0 + math.sin(phase) * 1e-6,
                1.0 + math.cos(phase) * 1e-6,
                1.0 + math.sin(phase * 2.0) * 1e-6,
            )
            obj.keyframe_insert("location", frame=frame)
            obj.keyframe_insert("rotation_euler", frame=frame)
            obj.keyframe_insert("scale", frame=frame)
        if obj.animation_data and obj.animation_data.action:
            obj.animation_data.action.name = f"COMMON_{name}"
            for fcurve in getattr(obj.animation_data.action, "fcurves", ()):
                for keyframe in fcurve.keyframe_points:
                    keyframe.interpolation = "LINEAR"
    scene.frame_set(FRAME_START)


def look_at(camera: bpy.types.Object, point: Vector) -> None:
    camera.rotation_euler = (point - camera.location).to_track_quat("-Z", "Y").to_euler()


def create_preview_camera() -> None:
    bpy.ops.object.camera_add(location=(-12.5, 15.5, 8.0))
    camera = bpy.context.object
    camera.name = "PREVIEW_CAMERA_NOT_EXPORTED"
    camera.data.lens = 52
    look_at(camera, Vector((0.0, 0.0, 1.3)))
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="AREA", location=(-5.0, 7.0, 10.0))
    key = bpy.context.object
    key.name = "PREVIEW_KEY_NOT_EXPORTED"
    key.data.energy = 900
    key.data.shape = "DISK"
    key.data.size = 8.0
    look_at(key, Vector((0.0, 0.0, 0.0)))
    bpy.ops.object.light_add(type="AREA", location=(7.0, 2.0, 4.0))
    fill = bpy.context.object
    fill.name = "PREVIEW_FILL_NOT_EXPORTED"
    fill.data.energy = 500
    fill.data.size = 6.0
    look_at(fill, Vector((0.0, 0.0, 0.0)))


def merge_door_primitives_to_three(
    document: dict,
    output: Path,
    spec: VehicleSpec,
    door_mesh: dict,
    atlas_stem: str = "driver-door",
) -> tuple[list[dict], tuple[str, ...]]:
    """Return one complete door SOBJ plus two invisible animation pads.

    HOME accepts the ClouDS-style three-member CANM dictionary, but treating
    the GTA door's material primitives as three independently rendered members
    still lets the pieces diverge on hardware.  Bake every door primitive --
    paint, trim, handle, mirror, interior and glass -- into one atlas-backed
    primitive.  The other two members are degenerate triangles: they preserve
    the proven three-member animation contract without visibly splitting the
    door.
    """

    primitives = list(door_mesh.get("primitives", []))
    if len(primitives) < 2:
        raise RuntimeError(
            f"unexpected complete-door primitive count: {len(primitives)}"
        )

    material_indices = []
    for primitive in primitives:
        material_index = primitive.get("material", 0)
        if material_index not in material_indices:
            material_indices.append(material_index)
    columns = 3
    rows = math.ceil(len(material_indices) / columns)
    # HOME Menu banners have a strict 512 KiB CGFX budget.  The door occupies
    # only a small part of the 400x240 render, so a compact atlas preserves the
    # three-member animation layout without pushing the VC banner over budget.
    # Preserve the denser LCS alloy wheel's texture budget.
    atlas_width = 128 if spec.game == "lcs" and atlas_stem.startswith("wheel-") else 64
    atlas_height = 64
    tile_width = atlas_width // columns
    tile_height = atlas_height // rows
    atlas_pixels = [0.0] * (atlas_width * atlas_height * 4)
    tile_by_material: dict[int, tuple[int, int]] = {}

    for tile_index, material_index in enumerate(material_indices):
        column = tile_index % columns
        row = tile_index // columns
        tile_by_material[material_index] = (column, row)
        material = document["materials"][material_index]
        material_name = material.get("name", "").casefold()
        pbr = material.get("pbrMetallicRoughness", {})
        factor = list(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]))
        if material_name == "primary" or material_name.startswith("primary."):
            factor = [*(value / 255.0 for value in spec.primary_rgb), 1.0]
        elif material_name == "secondary" or material_name.startswith("secondary."):
            factor = [*(value / 255.0 for value in spec.secondary_rgb), 1.0]

        source_pixels = None
        texture_info = pbr.get("baseColorTexture")
        if texture_info is not None:
            texture = document["textures"][texture_info["index"]]
            image_data = document["images"][texture["source"]]
            uri = image_data.get("uri")
            if uri:
                source = bpy.data.images.load(str(output.parent / uri), check_existing=False)
                resized = source.copy()
                resized.scale(tile_width, tile_height)
                source_pixels = list(resized.pixels)
                bpy.data.images.remove(resized)
                bpy.data.images.remove(source)

        for y in range(tile_height):
            for x in range(tile_width):
                destination = (
                    ((row * tile_height + y) * atlas_width + column * tile_width + x)
                    * 4
                )
                if source_pixels is None:
                    rgba = factor
                else:
                    source_offset = (y * tile_width + x) * 4
                    rgba = [
                        source_pixels[source_offset + channel] * factor[channel]
                        for channel in range(4)
                    ]
                # The complete door is one ordinary opaque SOBJ.  Preserve the
                # authored glass RGB as a dark window but do not let translucent
                # pixels make the exterior shell lose depth writes on HOME.
                rgba[3] = 1.0
                atlas_pixels[destination : destination + 4] = rgba

    atlas_name = f"{spec.game}-{atlas_stem}-atlas.png"
    atlas_path = output.parent / atlas_name
    atlas = bpy.data.images.new(
        f"{spec.game}_{atlas_stem.replace('-', '_')}_atlas",
        width=atlas_width,
        height=atlas_height,
        alpha=True,
    )
    atlas.pixels = atlas_pixels
    atlas.file_format = "PNG"
    atlas.filepath_raw = str(atlas_path)
    atlas.save()
    bpy.data.images.remove(atlas)

    document["images"].append({"uri": atlas_name})
    document["textures"].append({"source": len(document["images"]) - 1})
    document["materials"].append(
        {
            "name": f"{spec.game}_{atlas_stem.replace('-', '_')}_atlas",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": len(document["textures"]) - 1},
                # pycgfx multiplies texture0 by MaterialColor.Diffuse.  Its
                # implicit default is black, so this must be explicit or the
                # otherwise-correct atlas renders as a solid black door.
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "alphaMode": "OPAQUE",
            "doubleSided": False,
        }
    )
    atlas_material_index = len(document["materials"]) - 1

    buffer_data = document["buffers"][0]
    buffer_path = output.parent / buffer_data["uri"]
    payload = bytearray(buffer_path.read_bytes())
    component_info = {
        5121: ("B", 1),
        5123: ("H", 2),
        5125: ("I", 4),
        5126: ("f", 4),
    }
    component_counts = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

    def read_accessor(index: int) -> list[tuple[float, ...]]:
        accessor = document["accessors"][index]
        view = document["bufferViews"][accessor["bufferView"]]
        component_type = accessor["componentType"]
        code, component_size = component_info[component_type]
        count = component_counts[accessor["type"]]
        element_size = component_size * count
        stride = view.get("byteStride", element_size)
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [
            struct.unpack_from(
                "<" + code * count,
                payload,
                start + item * stride,
            )
            for item in range(accessor["count"])
        ]

    def append_accessor(
        values: list[tuple[float, ...]],
        component_type: int,
        accessor_type: str,
        *,
        include_bounds: bool = False,
    ) -> int:
        while len(payload) % 4:
            payload.append(0)
        offset = len(payload)
        code, _component_size = component_info[component_type]
        for value in values:
            payload.extend(struct.pack("<" + code * len(value), *value))
        document["bufferViews"].append(
            {
                "buffer": 0,
                "byteOffset": offset,
                "byteLength": len(payload) - offset,
            }
        )
        accessor = {
            "bufferView": len(document["bufferViews"]) - 1,
            "componentType": component_type,
            "count": len(values),
            "type": accessor_type,
        }
        if include_bounds:
            accessor["min"] = [
                min(value[axis] for value in values)
                for axis in range(len(values[0]))
            ]
            accessor["max"] = [
                max(value[axis] for value in values)
                for axis in range(len(values[0]))
            ]
        document["accessors"].append(accessor)
        return len(document["accessors"]) - 1

    positions: list[tuple[float, ...]] = []
    normals: list[tuple[float, ...]] = []
    texcoords: list[tuple[float, ...]] = []
    indices: list[tuple[int]] = []
    for primitive in primitives:
        attributes = primitive["attributes"]
        source_positions = read_accessor(attributes["POSITION"])
        source_normals = read_accessor(attributes["NORMAL"])
        source_texcoords = read_accessor(attributes["TEXCOORD_0"])
        source_indices = read_accessor(primitive["indices"])
        base_vertex = len(positions)
        column, row = tile_by_material[primitive.get("material", 0)]
        positions.extend(source_positions)
        normals.extend(source_normals)
        texcoords.extend(
            (
                (column * tile_width + uv[0] * tile_width) / atlas_width,
                (row * tile_height + uv[1] * tile_height) / atlas_height,
            )
            for uv in source_texcoords
        )
        indices.extend((base_vertex + int(index[0]),) for index in source_indices)
    complete_door = {
        "attributes": {
            "POSITION": append_accessor(
                positions, 5126, "VEC3", include_bounds=True
            ),
            "NORMAL": append_accessor(normals, 5126, "VEC3"),
            "TEXCOORD_0": append_accessor(texcoords, 5126, "VEC2"),
        },
        "indices": append_accessor(indices, 5123, "SCALAR"),
        "material": atlas_material_index,
        "mode": 4,
    }

    # Two zero-area triangles are deliberately invisible, yet each converts
    # to one rigid SOBJ and therefore keeps the three contiguous CANM members
    # required by HOME's banner player.
    pad_positions = append_accessor(
        [(0.0, 0.0, 0.0)] * 3, 5126, "VEC3", include_bounds=True
    )
    pad_normals = append_accessor([(0.0, 0.0, 1.0)] * 3, 5126, "VEC3")
    pad_texcoords = append_accessor([(0.0, 0.0)] * 3, 5126, "VEC2")
    pad_indices = append_accessor([(0,), (1,), (2,)], 5123, "SCALAR")
    invisible_pad = {
        "attributes": {
            "POSITION": pad_positions,
            "NORMAL": pad_normals,
            "TEXCOORD_0": pad_texcoords,
        },
        "indices": pad_indices,
        "material": atlas_material_index,
        "mode": 4,
    }

    buffer_data["byteLength"] = len(payload)
    buffer_path.write_bytes(payload)
    final_primitives = [complete_door, invisible_pad, invisible_pad]
    nodes = []
    for index, primitive in enumerate(final_primitives):
        name = f"{atlas_stem.replace('-', '_')}_{index:02d}"
        mesh_copy = dict(door_mesh)
        mesh_copy["name"] = name
        mesh_copy["primitives"] = [primitive]
        document["meshes"].append(mesh_copy)
        nodes.append({"mesh": len(document["meshes"]) - 1, "name": name})
    return nodes, tuple(node["name"] for node in nodes)


def combine_axles_as_skinned_mesh(
    document: dict,
    output: Path,
    nodes: list[dict],
    node_indices: dict[str, int],
) -> None:
    """Combine both axle meshes into CTGP-7's one two-joint wheel SOBJ."""

    buffer_data = document["buffers"][0]
    buffer_path = output.parent / buffer_data["uri"]
    payload = bytearray(buffer_path.read_bytes())
    component_info = {
        5121: ("B", 1),
        5123: ("H", 2),
        5125: ("I", 4),
        5126: ("f", 4),
    }
    component_counts = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
        "MAT4": 16,
    }

    def read_accessor(index: int) -> list[tuple[float, ...]]:
        accessor = document["accessors"][index]
        view = document["bufferViews"][accessor["bufferView"]]
        code, component_size = component_info[accessor["componentType"]]
        count = component_counts[accessor["type"]]
        element_size = component_size * count
        stride = view.get("byteStride", element_size)
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [
            struct.unpack_from(
                "<" + code * count,
                payload,
                start + item * stride,
            )
            for item in range(accessor["count"])
        ]

    def append_accessor(
        values: list[tuple[float, ...]],
        component_type: int,
        accessor_type: str,
        *,
        include_bounds: bool = False,
    ) -> int:
        while len(payload) % 4:
            payload.append(0)
        offset = len(payload)
        code, _size = component_info[component_type]
        for value in values:
            payload.extend(struct.pack("<" + code * len(value), *value))
        document["bufferViews"].append(
            {
                "buffer": 0,
                "byteOffset": offset,
                "byteLength": len(payload) - offset,
            }
        )
        accessor = {
            "bufferView": len(document["bufferViews"]) - 1,
            "componentType": component_type,
            "count": len(values),
            "type": accessor_type,
        }
        if include_bounds:
            accessor["min"] = [
                min(value[axis] for value in values)
                for axis in range(len(values[0]))
            ]
            accessor["max"] = [
                max(value[axis] for value in values)
                for axis in range(len(values[0]))
            ]
        document["accessors"].append(accessor)
        return len(document["accessors"]) - 1

    positions = []
    normals = []
    texcoords = []
    joints = []
    weights = []
    indices = []
    material_indices = []
    pivots = []
    for joint_index, axle_name in enumerate(("wheel_front", "wheel_rear")):
        node = nodes[node_indices[axle_name]]
        pivot = tuple(node.get("translation", (0.0, 0.0, 0.0)))
        pivots.append(pivot)
        mesh = document["meshes"][node["mesh"]]
        if len(mesh.get("primitives", [])) != 1:
            raise RuntimeError(f"{axle_name} is not one atlas primitive")
        primitive = mesh["primitives"][0]
        attrs = primitive["attributes"]
        source_positions = read_accessor(attrs["POSITION"])
        source_normals = read_accessor(attrs["NORMAL"])
        source_texcoords = read_accessor(attrs["TEXCOORD_0"])
        source_indices = read_accessor(primitive["indices"])
        base_vertex = len(positions)
        # CTGP's mode-1 Wheel_D stream stores each rigidly skinned axle group
        # in that axle bone's local space.  The bone translation places it at
        # the car; baking the same pivot into POSITION makes the wheel centre
        # orbit around an otherwise-correct axle on HOME.
        positions.extend(tuple(value) for value in source_positions)
        normals.extend(source_normals)
        texcoords.extend(source_texcoords)
        joints.extend([(joint_index, 0, 0, 0)] * len(source_positions))
        weights.extend([(1.0, 0.0, 0.0, 0.0)] * len(source_positions))
        indices.extend(
            (base_vertex + int(value[0]),) for value in source_indices
        )
        material_indices.append(primitive["material"])
        node.pop("mesh")

    def material_image(material_index: int) -> Path:
        material = document["materials"][material_index]
        texture_index = material["pbrMetallicRoughness"]["baseColorTexture"][
            "index"
        ]
        image_index = document["textures"][texture_index]["source"]
        return output.parent / document["images"][image_index]["uri"]

    front_image, rear_image = map(material_image, material_indices)
    if front_image.read_bytes() != rear_image.read_bytes():
        raise RuntimeError("front and rear wheel atlases unexpectedly differ")

    combined_primitive = {
        "attributes": {
            "POSITION": append_accessor(
                positions, 5126, "VEC3", include_bounds=True
            ),
            "NORMAL": append_accessor(normals, 5126, "VEC3"),
            "TEXCOORD_0": append_accessor(texcoords, 5126, "VEC2"),
            "JOINTS_0": append_accessor(joints, 5121, "VEC4"),
            "WEIGHTS_0": append_accessor(weights, 5126, "VEC4"),
        },
        "indices": append_accessor(indices, 5123, "SCALAR"),
        "material": material_indices[0],
        "mode": 4,
    }
    document["meshes"].append(
        {"name": "wheel_mesh", "primitives": [combined_primitive]}
    )

    inverse_bind_matrices = []
    for x, y, z in pivots:
        inverse_bind_matrices.append(
            (
                1.0,
                0.0,
                0.0,
                0.0,
                0.0,
                1.0,
                0.0,
                0.0,
                0.0,
                0.0,
                1.0,
                0.0,
                -x,
                -y,
                -z,
                1.0,
            )
        )
    document.setdefault("skins", []).append(
        {
            "name": "wheel_skin",
            "inverseBindMatrices": append_accessor(
                inverse_bind_matrices, 5126, "MAT4"
            ),
            "joints": [
                node_indices["wheel_front"],
                node_indices["wheel_rear"],
            ],
            "skeleton": node_indices["vehicle_motion"],
        }
    )
    nodes.append(
        {
            "name": "wheel_mesh",
            "mesh": len(document["meshes"]) - 1,
            "skin": len(document["skins"]) - 1,
        }
    )
    nodes[node_indices["vehicle_motion"]].setdefault("children", []).append(
        len(nodes) - 1
    )
    buffer_data["byteLength"] = len(payload)
    buffer_path.write_bytes(payload)


def gltf_material_uses_lcs_paint(document: dict, material: dict) -> bool:
    texture_info = material.get("pbrMetallicRoughness", {}).get(
        "baseColorTexture"
    )
    if texture_info is None:
        return False
    texture = document["textures"][texture_info["index"]]
    image = document["images"][texture["source"]]
    image_name = " ".join(
        str(image.get(key, "")).casefold() for key in ("name", "uri")
    )
    return "xv_vehiclegrunge" in image_name or "xv_bodypanels" in image_name


def export_gltf(output: Path, spec: VehicleSpec) -> None:
    bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLTF_SEPARATE",
        export_animations=True,
        export_animation_mode="ACTIVE_ACTIONS",
        export_nla_strips_merged_animation_name="COMMON",
        export_frame_range=True,
        export_frame_step=1,
        export_force_sampling=True,
        export_skins=True,
        export_cameras=False,
        export_lights=False,
    )
    document = json.loads(output.read_text(encoding="utf-8"))

    # Final fallback is deliberately static.  HOME hardware repeatedly mapped
    # otherwise-valid rigid CANM rotation onto the complete banner root.  Keep
    # the accepted composition/material pipeline but emit no animation data.
    static_names = {
        "vehicle_body",
        "native_car_shadow",
        "world",
        "COMMON",
    }
    exported_names = {node.get("name") for node in document.get("nodes", [])}
    if "name" in exported_names:
        static_names.add("name")
    if exported_names == static_names:
        document.pop("animations", None)
        for material in document.get("materials", []):
            name = material.get("name", "").casefold()
            if (
                name == "primary"
                or name.startswith("primary.")
                or (spec.game == "lcs" and gltf_material_uses_lcs_paint(document, material))
            ):
                material.setdefault("pbrMetallicRoughness", {})[
                    "baseColorFactor"
                ] = [*(channel / 255.0 for channel in spec.primary_rgb), 1.0]
            elif name == "secondary" or name.startswith("secondary."):
                material.setdefault("pbrMetallicRoughness", {})[
                    "baseColorFactor"
                ] = [*(channel / 255.0 for channel in spec.secondary_rgb), 1.0]
            if name.startswith("glass") or name == "native_shad_car":
                material["alphaMode"] = "BLEND"
                material["doubleSided"] = True
            elif name == "banner_logo_alpha":
                material["alphaMode"] = "MASK"
                material["alphaCutoff"] = 0.08
                material["doubleSided"] = True
            else:
                material["alphaMode"] = "OPAQUE"
                material.pop("alphaCutoff", None)
                material["doubleSided"] = False
        output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        return

    nodes = document.get("nodes", [])
    node_indices = {node.get("name"): index for index, node in enumerate(nodes)}
    expected_old_names = {
        "body_bob",
        "vehicle_motion",
        "wheel_front",
        "wheel_rear",
        "native_car_shadow",
        "world",
        "COMMON",
    }
    if "name" in node_indices:
        expected_old_names.add("name")
    if set(node_indices) != expected_old_names or len(nodes) != len(
        expected_old_names
    ):
        raise RuntimeError(
            "unexpected exported node set: "
            + ", ".join(str(node.get("name")) for node in nodes)
        )

    # Each animated axle must bind one and only one SOBJ.  Their imported wheel
    # meshes use separate tyre/rim materials, so bake those primitives into a
    # compact atlas without touching the now-static, fully integrated body.
    for axle_name in ("wheel_front", "wheel_rear"):
        node = nodes[node_indices[axle_name]]
        source_mesh = document["meshes"][node["mesh"]]
        merged_nodes, _unused_names = merge_door_primitives_to_three(
            document,
            output,
            spec,
            source_mesh,
            atlas_stem=axle_name.replace("_", "-"),
        )
        node["mesh"] = merged_nodes[0]["mesh"]

    combine_axles_as_skinned_mesh(
        document, output, nodes, node_indices
    )
    node_indices = {node.get("name"): index for index, node in enumerate(nodes)}

    for animation in document.get("animations", []):
        animation["name"] = "COMMON"
        old_samplers = animation.get("samplers", [])
        source_channels = {
            (
                nodes[channel.get("target", {}).get("node")].get("name"),
                channel.get("target", {}).get("path"),
            ): channel
            for channel in animation.get("channels", [])
            if isinstance(channel.get("target", {}).get("node"), int)
        }
        # Match CTGP-7's sparse CANM contract exactly: the vehicle parent has
        # one translation track and each child axle has one rotation track.
        # HOME rejects or mis-resolves the nine forced TRS tracks used by the
        # earlier diagnostic banners.
        expected_channels = {
            ("body_bob", "translation"),
            ("body_bob", "rotation"),
            ("wheel_front", "rotation"),
            ("wheel_rear", "rotation"),
        }
        if not expected_channels.issubset(source_channels):
            missing = sorted(expected_channels - set(source_channels))
            raise RuntimeError(f"missing high-speed animation channels: {missing}")
        new_channels = []
        new_samplers = []
        for member, path in (
            ("body_bob", "translation"),
            ("body_bob", "rotation"),
            ("wheel_front", "rotation"),
            ("wheel_rear", "rotation"),
        ):
            source = source_channels[(member, path)]
            sampler = dict(old_samplers[source["sampler"]])
            sampler["interpolation"] = "LINEAR"
            new_channels.append(
                {
                    "sampler": len(new_samplers),
                    "target": {
                        "node": node_indices[member],
                        "path": path,
                    },
                }
            )
            new_samplers.append(sampler)
        animation["channels"] = new_channels
        animation["samplers"] = new_samplers

    for material in document.get("materials", []):
        name = material.get("name", "").casefold()
        if (
            name == "primary"
            or name.startswith("primary.")
            or (spec.game == "lcs" and gltf_material_uses_lcs_paint(document, material))
        ):
            material.setdefault("pbrMetallicRoughness", {})["baseColorFactor"] = [
                *(channel / 255.0 for channel in spec.primary_rgb),
                1.0,
            ]
        elif name == "secondary" or name.startswith("secondary."):
            material.setdefault("pbrMetallicRoughness", {})["baseColorFactor"] = [
                *(channel / 255.0 for channel in spec.secondary_rgb),
                1.0,
            ]
        if name.startswith("glass"):
            material["alphaMode"] = "BLEND"
            material["doubleSided"] = True
        elif name == "native_shad_car":
            material["alphaMode"] = "BLEND"
            material["doubleSided"] = True
        elif name == "banner_logo_alpha":
            # Match the depth-writing ClouDS logo path.  BLEND is sorted after
            # the vehicle by HOME and visibly draws through the bodywork.
            material["alphaMode"] = "MASK"
            material["alphaCutoff"] = 0.08
            material["doubleSided"] = True
        else:
            # DragonFF materials often arrive with alpha blending enabled even
            # when their source DFF surface is ordinary opaque bodywork.  HOME
            # Menu then suppresses depth writes and the outer shell disappears,
            # exposing the cabin.  Texture alpha must never affect those parts.
            material["alphaMode"] = "OPAQUE"
            material.pop("alphaCutoff", None)
            material["doubleSided"] = False

    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def render_previews(output_dir: Path) -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    # These are technical colour previews, not cinematic renders.  AgX rolls
    # the muted mission colours into a much darker grey-green, making an exact
    # carcols.dat value look wrong even though the exported glTF is correct.
    # Standard keeps the neutral-lit body close to its authored sRGB swatch.
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    scene.render.resolution_x = 500
    scene.render.resolution_y = 300
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = True
    for frame, name in (
        (FRAME_START, "closed"),
        (OPEN_HOLD_END, "open"),
    ):
        scene.frame_set(frame)
        scene.render.filepath = str(output_dir / f"preview-{name}.png")
        bpy.ops.render.render(write_still=True)


def main() -> None:
    dragonff_root, output_dir, game = arguments()
    output_dir.mkdir(parents=True, exist_ok=True)
    source_dir = output_dir / "source"
    spec = SPECS[game]
    sys.path.insert(0, str(dragonff_root.parent))
    import DragonFF  # type: ignore[import-not-found]
    from DragonFF.ops.dff_importer import import_dff  # type: ignore[import-not-found]
    from DragonFF.ops.txd_importer import import_txd  # type: ignore[import-not-found]

    DragonFF.register()
    clear_scene()
    txd_images = {}
    for name in (
        "generic.txd",
        spec.txd_name,
        spec.wheel_txd_name,
        "particle.txd",
    ):
        txd_images.update(import_txd_casefold(import_txd, source_dir / name))
    import_dff_scene(import_dff, source_dir / spec.dff_name, txd_images)
    vehicle = bpy.data.objects.get(spec.model_name)
    if vehicle is None:
        raise RuntimeError(f"missing imported root {spec.model_name}")
    remove_damage_and_low_detail()
    add_wheels(import_dff, source_dir, txd_images, spec)
    common, world = create_roots(vehicle)
    shadow_images = txd_images.get("shad_car") or txd_images.get("shad_car".upper())
    if not shadow_images:
        raise RuntimeError("particle.txd is missing shad_car")
    create_shadow(world, shadow_images[0])
    apply_mission_colours(spec)
    consolidate_materials()
    vehicle_motion, front_axle, rear_axle = flatten_vehicle_for_high_speed_motion(world)
    prune_leaf_empties({"COMMON", "world"})
    animate_high_speed_motion(vehicle_motion, front_axle, rear_axle)

    create_preview_camera()
    render_previews(output_dir)
    checkpoint = output_dir / "banner-no-logo.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(checkpoint))
    # Blender 5.1's glTF depsgraph can retain the pre-join DFF objects until a
    # file reload even though the saved scene already contains only the three
    # flattened mesh nodes.  Reload the checkpoint so those stale instances
    # cannot leak back into the exported skeleton.
    bpy.ops.wm.open_mainfile(filepath=str(checkpoint))
    export_gltf(output_dir / "banner-no-logo.gltf", spec)
    print(
        f"built {game}: {spec.model_name}, mission colours "
        f"{spec.primary_rgb}/{spec.secondary_rgb}, 30 FPS frames "
        f"{MOTION_FRAME_START}..{MOTION_LOOP_END}; static doors, "
        "CTGP-7-style body/axle loop"
    )


if __name__ == "__main__":
    main()
