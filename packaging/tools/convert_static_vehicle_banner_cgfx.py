#!/usr/bin/env python3
"""Convert the final animation-free GTA vehicle banner to CGFX."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


SIZE_LIMIT = 512 * 1024


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_gltf", type=Path)
    parser.add_argument("output_cgfx", type=Path)
    parser.add_argument(
        "--pycgfx-root",
        type=Path,
        default=Path("banner_work/.tools/pycgfx"),
    )
    args = parser.parse_args()
    pycgfx_root = args.pycgfx_root.resolve()
    sys.path.insert(0, str(pycgfx_root))
    import gltflib  # type: ignore[import-not-found]
    import main as pycgfx  # type: ignore[import-not-found]
    from cgfx.mtob import (  # type: ignore[import-not-found]
        ColorFloat,
        FragmentLightingFlags,
        MTOBFlag,
    )
    from cgfx.primitives import VertexAttributeUsage  # type: ignore[import-not-found]
    from cgfx.sobj import BillboardMode  # type: ignore[import-not-found]

    gltf = gltflib.GLTF.load(
        str(args.input_gltf.resolve()), load_file_resources=True
    )
    if gltf.model.animations:
        raise SystemExit("static fallback must not contain glTF animations")
    cgfx = pycgfx.convert_gltf(gltf)
    if list(cgfx.data.models) != ["COMMON"]:
        raise SystemExit(f"unexpected models: {list(cgfx.data.models)}")
    if list(cgfx.data.skeletal_animations):
        raise SystemExit("static fallback unexpectedly contains CANM")
    model = cgfx.data.models["COMMON"]
    desired = [
        "Scene root",
        "COMMON",
        "name",
        "world",
        "vehicle_body",
        "native_car_shadow",
    ]
    current = list(model.skeleton.bones)
    if set(current) != set(desired):
        raise SystemExit("unexpected static skeleton: " + ", ".join(current))
    old_index = {name: index for index, name in enumerate(current)}
    nodes = model.skeleton.bones.dict.nodes
    sentinel = nodes[0]
    by_name = {node.name: node for node in nodes[1:]}
    model.skeleton.bones.dict.nodes = [
        sentinel,
        *(by_name[name] for name in desired),
    ]
    new_index = {name: index for index, name in enumerate(desired)}
    old_to_new = {old_index[name]: new_index[name] for name in desired}
    for name in desired:
        bone = model.skeleton.bones[name]
        bone.joint_id = new_index[name]
        if getattr(bone, "parent", None) is not None:
            bone.parent_id = new_index[bone.parent.name]
    for shape in model.shapes.data.contents:
        for primitive_set in shape.primitive_sets.data.contents:
            primitive_set.related_bones.data.contents = [
                old_to_new[index]
                for index in primitive_set.related_bones.data.contents
            ]
    skeletal_group = model.animation_group_descriptions.dict["SkeletalAnimation"]
    group_nodes = skeletal_group.members.dict.nodes
    group_sentinel = group_nodes[0]
    group_by_name = {node.name: node for node in group_nodes[1:]}
    skeletal_group.members.dict.nodes = [
        group_sentinel,
        *(group_by_name[name] for name in desired),
    ]
    bone_names = list(model.skeleton.bones)
    if model.skeleton.bones["vehicle_body"].parent_id != bone_names.index("world"):
        raise SystemExit("vehicle_body must be a direct child of world")
    if model.skeleton.bones["native_car_shadow"].parent_id != bone_names.index("world"):
        raise SystemExit("native_car_shadow must be a direct child of world")
    model.skeleton.bones["name"].billboard_mode = BillboardMode.YAxial

    owners: list[str] = []
    for mesh in model.meshes.data.contents:
        shape = model.shapes.data.contents[mesh.shape_index]
        primitive_sets = shape.primitive_sets.data.contents
        if len(primitive_sets) != 1:
            raise SystemExit(f"{shape.name} has multiple primitive sets")
        primitive_set = primitive_sets[0]
        if primitive_set.skinning_mode != 0:
            raise SystemExit(f"{shape.name} is skinned")
        related = list(primitive_set.related_bones.data.contents)
        if len(related) != 1:
            raise SystemExit(f"{shape.name} has non-rigid bones {related}")
        owner = bone_names[related[0]]
        if owner not in {"name", "vehicle_body", "native_car_shadow"}:
            raise SystemExit(f"unexpected static SOBJ owner {owner}")
        mesh.mesh_node_name = owner
        owners.append(owner)
        for attribute in shape.vertex_attributes.data.contents:
            streams = (
                attribute.vertex_streams.data.contents
                if hasattr(attribute, "vertex_streams")
                else (attribute,)
            )
            for stream in streams:
                if stream.usage in (
                    VertexAttributeUsage.BoneIndex,
                    VertexAttributeUsage.BoneWeight,
                ):
                    raise SystemExit(f"{shape.name} contains skin attributes")
    if owners.count("name") != 1 or "vehicle_body" not in owners:
        raise SystemExit(f"invalid static SOBJ bindings: {owners}")

    for material_name in model.materials:
        material = model.materials[material_name]
        material.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
        specular = material.fragment_shader.texture_combiners[2]
        specular.src_rgb = 0xFFF
        specular.combine_rgb = 0
        material.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
        material.fragment_shader.fragment_lighting_table.distribution_0_sampler = None
        if material_name == "banner_logo_alpha":
            for stage in material.fragment_shader.texture_combiners[1:3]:
                stage.src_rgb = 0xFFF
                stage.combine_rgb = 0
            material.flags = MTOBFlag(0)

    payload = pycgfx.write(cgfx)
    if len(payload) >= SIZE_LIMIT:
        raise SystemExit(f"CGFX is {len(payload)} bytes; limit is {SIZE_LIMIT}")
    output = args.output_cgfx.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    print(
        f"wrote {output} ({len(payload)} bytes); static integrated vehicle, "
        f"CANM=0, SOBJ bindings={len(owners)}"
    )


if __name__ == "__main__":
    main()
