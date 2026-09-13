"""Add a fixed rear logo and compose the complete GTA vehicle banner."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


LOGO_LAYOUT = {
    # Physical HOME-menu safe-area correction: move both complete compositions
    # down by the same eight-pixel-equivalent offset.  Keep the accepted
    # GTA3/VC logo-to-vehicle spacing and their different logo sizes intact.
    "gta3": (10.0, 4.0),
    "vc": (9.5, 4.3),
    "lcs": (10.0, 4.0),
}
LOGO_REAR_CLEARANCE = 0.6
# Match the proven ClouDS composition: the fixed logo is a normal depth-tested
# card behind the subject, while both visual centres share x=0.  The previous
# coplanar priority trick still intersected on HOME Menu.
LOGO_DEPTH = -3.0
VEHICLE_WORLD_Z = -3.9
VEHICLE_SCALE_FACTOR = 1.45
HOME_CAMERA_DISTANCE = 44.786
HOME_CAMERA_YFOV_DEGREES = 30.0


def arguments() -> tuple[Path, Path, Path, Path, str]:
    separator = sys.argv.index("--")
    args = sys.argv[separator + 1 :]
    if len(args) != 5 or args[4] not in {"gta3", "vc", "lcs"}:
        raise SystemExit(
            "expected SOURCE_BLEND LOGO_PNG OUTPUT_DIR BUILD_TOOLS_DIR "
            "gta3|vc|lcs after --"
        )
    return (
        Path(args[0]).resolve(),
        Path(args[1]).resolve(),
        Path(args[2]).resolve(),
        Path(args[3]).resolve(),
        args[4],
    )


def create_logo(
    common: bpy.types.Object,
    logo_path: Path,
    plane_size: float,
    centre_z: float,
    depth_y: float,
) -> bpy.types.Object:
    half = plane_size * 0.5
    bottom = centre_z - half
    top = centre_z + half
    # Match the proven ClouDS node contract exactly: position, size and facing
    # are baked into the card while the special root-level ``name`` node stays
    # identity-transformed.  With name at joint 2 and world at joint 3, HOME's
    # YAxial path cancels both orientation and screen displacement.
    # Match the final ClouDS card's vertex order, winding, normal and UV axes.
    vertices = [
        (-half, depth_y, bottom),
        (half, depth_y, bottom),
        (-half, depth_y, top),
        (half, depth_y, top),
    ]
    mesh = bpy.data.meshes.new("banner_logo_mesh")
    # Face +Y in Blender, which exports as ClouDS' -Z front normal.  The
    # previous -Y winding made HOME and the preview sample the mirrored back.
    mesh.from_pydata(vertices, [], [(0, 2, 3, 1)])
    uv = mesh.uv_layers.new(name="BANNER_LOGO_UV")
    for loop, coordinates in zip(
        # Blender's technical preview sees the front winding, while HOME's
        # YAxial billboard path presents the opposite horizontal convention.
        # Hardware evidence wins: flip U so the HOME Menu logo reads normally.
        uv.data, ((0, 0), (0, 1), (1, 1), (1, 0))
    ):
        loop.uv = coordinates

    logo = bpy.data.objects.new("name", mesh)
    bpy.context.scene.collection.objects.link(logo)
    logo.parent = common

    material = bpy.data.materials.new("banner_logo_alpha")
    material.use_nodes = True
    material.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    material.use_backface_culling = False
    if hasattr(material, "surface_render_method"):
        material.surface_render_method = "DITHERED"
    if hasattr(material, "alpha_threshold"):
        material.alpha_threshold = 0.08
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
    texture.name = "Banner Logo Texture"
    texture.image = bpy.data.images.load(str(logo_path), check_existing=True)
    texture.interpolation = "Linear"
    links.new(texture.outputs["Color"], principled.inputs["Base Color"])
    links.new(texture.outputs["Alpha"], principled.inputs["Alpha"])
    emission = principled.inputs.get("Emission Color")
    emission_strength = principled.inputs.get("Emission Strength")
    if emission is not None and emission_strength is not None:
        links.new(texture.outputs["Color"], emission)
        emission_strength.default_value = 0.7
    principled.inputs["Metallic"].default_value = 0.0
    principled.inputs["Roughness"].default_value = 1.0
    mesh.materials.append(material)
    return logo


def look_at(obj: bpy.types.Object, point: Vector) -> None:
    obj.rotation_euler = (point - obj.location).to_track_quat("-Z", "Y").to_euler()


def configure_home_preview(output_dir: Path) -> None:
    old_camera = bpy.data.objects.get("PREVIEW_CAMERA_NOT_EXPORTED")
    if old_camera is not None:
        bpy.data.objects.remove(old_camera, do_unlink=True)
    bpy.ops.object.camera_add(location=(0.0, HOME_CAMERA_DISTANCE, 1.0))
    camera = bpy.context.object
    camera.name = "HOME_BANNER_CAMERA_NOT_EXPORTED"
    camera.data.angle_y = math.radians(HOME_CAMERA_YFOV_DEGREES)
    look_at(camera, Vector((0.0, 0.0, 1.0)))
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 500
    scene.render.resolution_y = 300
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    for frame, name in ((0, "closed"), (75, "open")):
        scene.frame_set(frame)
        scene.render.filepath = str(output_dir / f"preview-complete-{name}.png")
        bpy.ops.render.render(write_still=True)


def replace_lcs_texture(output_dir: Path, image_name: str, file_name: str) -> None:
    texture_path = output_dir / file_name
    if not texture_path.is_file():
        raise RuntimeError(f"missing LCS replacement texture: {texture_path}")
    image = bpy.data.images.get(image_name)
    if image is None:
        raise RuntimeError(f"source scene is missing {image_name}")
    if image.packed_file is not None:
        image.unpack(method="REMOVE")
    image.filepath = str(texture_path)
    image.source = "FILE"
    image.reload()
    image.pack()


def remove_lcs_texture(image_name: str) -> None:
    image = bpy.data.images.get(image_name)
    if image is None:
        raise RuntimeError(f"source scene is missing {image_name}")
    removed = 0
    for material in bpy.data.materials:
        if not material.use_nodes or material.node_tree is None:
            continue
        uses_image = any(
            node.type == "TEX_IMAGE" and node.image == image
            for node in material.node_tree.nodes
        )
        if not uses_image:
            continue
        paint = tuple(channel / 255.0 for channel in (15, 15, 15))
        material.diffuse_color = (*paint, 1.0)
        for node in material.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                node.inputs["Base Color"].default_value = (*paint, 1.0)
        for node in list(material.node_tree.nodes):
            if node.type == "TEX_IMAGE" and node.image == image:
                material.node_tree.nodes.remove(node)
                removed += 1
    if removed == 0:
        raise RuntimeError(f"LCS texture {image_name} is not used by a material")
    bpy.data.images.remove(image)


def make_flat_material(name: str, rgb: tuple[float, float, float]) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.diffuse_color = (*rgb, 1.0)
    principled = next(
        (node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"),
        None,
    )
    if principled is None:
        raise RuntimeError(f"material {name} is missing Principled BSDF")
    principled.inputs["Base Color"].default_value = (*rgb, 1.0)
    principled.inputs["Roughness"].default_value = 0.72
    return material


def add_lcs_door_handle_highlights() -> None:
    """Add tiny texture-free handle rims that remain legible on black paint."""
    import bmesh

    body = bpy.data.objects.get("body_bob")
    if body is None or body.type != "MESH":
        raise RuntimeError("LCS source scene is missing the body_bob mesh")

    highlight = make_flat_material("door_handle_highlight", (0.42, 0.42, 0.42))
    inset = make_flat_material("door_handle_inset", (0.025, 0.025, 0.025))
    highlight_index = len(body.data.materials)
    body.data.materials.append(highlight)
    inset_index = len(body.data.materials)
    body.data.materials.append(inset)

    mesh = body.data
    bm = bmesh.new()
    bm.from_mesh(mesh)

    def add_quad(
        side: float,
        centre_y: float,
        centre_z: float,
        half_length: float,
        half_height: float,
        outward_offset: float,
        material_index: int,
    ) -> None:
        x = side * (1.022 + outward_offset)
        coords = [
            (x, centre_y - half_length, centre_z - half_height),
            (x, centre_y + half_length, centre_z - half_height),
            (x, centre_y + half_length, centre_z + half_height),
            (x, centre_y - half_length, centre_z + half_height),
        ]
        if side < 0.0:
            coords.reverse()
        face = bm.faces.new([bm.verts.new(coord) for coord in coords])
        face.material_index = material_index

    for side in (-1.0, 1.0):
        for centre_y in (0.42, -0.72):
            # The upper edge of the door sheet is around z=0.239; keep the
            # entire handle below it instead of placing it in the side glass.
            add_quad(side, centre_y, 0.12, 0.12, 0.035, 0.0, highlight_index)
            add_quad(side, centre_y, 0.12, 0.085, 0.014, 0.002, inset_index)

    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def main() -> None:
    source_blend, logo_path, output_dir, build_tools_dir, game = arguments()
    output_dir.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(source_blend))
    common = bpy.data.objects.get("COMMON")
    world = bpy.data.objects.get("world")
    if common is None or world is None or world.parent != common:
        raise RuntimeError("source scene is missing COMMON/world")
    if bpy.data.objects.get("name") is not None:
        raise RuntimeError("source scene already contains a name logo node")
    if game == "lcs":
        replace_lcs_texture(output_dir, "particle_shad_car_0", "particle_shad_car_0.png")
        # The subtle grunge mask is not worth a dedicated HOME texture.  Keep
        # its car-body faces/material and let the existing flat paint colour
        # feed the shader directly, rather than exporting a missing reference.
        remove_lcs_texture("generic_xv_vehiclegrunge_0.001")
        add_lcs_door_handle_highlights()

    world.location.z = VEHICLE_WORLD_Z
    world.rotation_mode = "XYZ"
    world.rotation_euler = (0.0, 0.0, 0.0)
    world.scale = tuple(value * VEHICLE_SCALE_FACTOR for value in world.scale)
    bpy.context.view_layer.update()
    # Copy the accepted ClouDS composition: subject and logo share the same
    # horizontal centre line, and the logo is a smaller, ordinary card behind
    # the subject rather than a special coplanar render layer.
    logo_depth = LOGO_DEPTH
    logo_size, logo_centre = LOGO_LAYOUT[game]
    create_logo(
        common,
        logo_path,
        logo_size,
        logo_centre,
        logo_depth,
    )
    configure_home_preview(output_dir)

    # Save at the closed loop boundary rather than leaving the editable blend
    # at the last rendered open-door frame.
    bpy.context.scene.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=str(output_dir / "banner.blend"))

    sys.path.insert(0, str(build_tools_dir))
    import build_vehicle_banner  # type: ignore[import-not-found]

    gltf_path = output_dir / "banner.gltf"
    build_vehicle_banner.export_gltf(
        gltf_path, build_vehicle_banner.SPECS[game]
    )

    import json

    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    logo_found = False
    for material in document.get("materials", []):
        if game == "lcs" and material.get("name") == "chassis_hi.3":
            # Blender omits baseColorFactor after the grunge image node is
            # removed, which glTF defines as white.  Preserve Leone's actual
            # CARCOLS 15,15,15 paint explicitly for the texture-free faces.
            material.setdefault("pbrMetallicRoughness", {})[
                "baseColorFactor"
            ] = [15.0 / 255.0, 15.0 / 255.0, 15.0 / 255.0, 1.0]
        if material.get("name") != "banner_logo_alpha":
            continue
        # The title billboard is intentionally above the vehicle, so HOME's
        # translucent-node sorting does not create the old draw-through issue
        # in these three compositions.  Use ordinary alpha blending for the
        # authored antialiased edge; changing MASK's cutoff produced banners
        # that froze HOME on hardware.
        material["alphaMode"] = "BLEND"
        material.pop("alphaCutoff", None)
        material["doubleSided"] = True
        logo_found = True
    if not logo_found:
        raise RuntimeError("exported scene is missing banner_logo_alpha")
    gltf_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(
        f"assembled {game}: fixed rear logo at y={logo_depth:.3f}, "
        f"centre z={logo_centre}, plane={logo_size}; "
        f"vehicle z={VEHICLE_WORLD_Z}, baked yaw="
        f"{build_vehicle_banner.DISPLAY_YAW_DEGREES}, "
        f"scale factor={VEHICLE_SCALE_FACTOR}"
    )


if __name__ == "__main__":
    main()
