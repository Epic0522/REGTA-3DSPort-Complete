#!/usr/bin/env python3
"""Convert a rigid vehicle banner glTF into a hardware-safe CGFX.

pycgfx creates the correct skeleton relationship for each primitive, but it
leaves SOBJMesh.mesh_node_name empty.  HOME Menu uses that string for rigid
mesh-node lookup, so bind every primitive to the single bone already recorded
in its PrimitiveSet rather than guessing from pycgfx's unstable mesh names.
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from collections import Counter
from pathlib import Path


SIZE_LIMIT = 512 * 1024
WHEEL_ROTATIONS_PER_LOOP = 12.0
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_gltf", type=Path)
    parser.add_argument("output_cgfx", type=Path)
    parser.add_argument(
        "--pycgfx-root",
        type=Path,
        default=Path("banner_work/.tools/pycgfx"),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    pycgfx_root = args.pycgfx_root.resolve()
    if not (pycgfx_root / "main.py").is_file():
        raise SystemExit(f"pycgfx was not found at {pycgfx_root}")

    sys.path.insert(0, str(pycgfx_root))
    import gltflib  # type: ignore[import-not-found]
    import main as pycgfx  # type: ignore[import-not-found]
    from cgfx.canm import (  # type: ignore[import-not-found]
        CANMBoneTransform,
        FloatAnimationCurve,
        FloatSegment,
        Hermite128Key,
        InterpolationType,
        PrimitiveType,
        QuantizationType,
        StepLinear64Key,
    )
    from cgfx.mtob import (  # type: ignore[import-not-found]
        ColorFloat,
        FragmentLightingFlags,
        MTOBFlag,
    )
    from cgfx.primitives import (  # type: ignore[import-not-found]
        InterleavedVertexStream,
        VertexAttributeUsage,
    )
    from cgfx.shared import (  # type: ignore[import-not-found]
        Matrix,
        StandardObject,
        Vector4,
    )
    from cgfx.sobj import (  # type: ignore[import-not-found]
        BillboardMode,
        BoneFlag,
        SkeletonFlag,
        SkeletonScalingRule,
    )

    class MeshNodeVisibility(StandardObject):
        """Official CMDL mesh-node dictionary value: name pointer + visible."""

        struct = struct.Struct("iI")

        def __init__(self, name: str, visible: bool = True) -> None:
            self.name = name
            self.visible = visible

        def values(self) -> tuple:
            return (self.name, self.visible)

    input_path = args.input_gltf.resolve()
    gltf = gltflib.GLTF.load(str(input_path), load_file_resources=True)
    cgfx = pycgfx.convert_gltf(gltf)

    model_names = list(cgfx.data.models)
    if model_names != ["COMMON"]:
        raise SystemExit(f"expected only model COMMON, got {model_names}")
    model = cgfx.data.models["COMMON"]
    if model is None or getattr(model, "skeleton", None) is None:
        raise SystemExit("COMMON model has no skeleton")

    bone_names = list(model.skeleton.bones)
    for required in ("COMMON", "world", "name"):
        if required not in bone_names:
            raise SystemExit(f"COMMON skeleton is missing {required}")
    # CTGP-7's installed, working banner uses a moving body bone followed by
    # two child axle bones.  Reproduce that proven order and sparse-track
    # contract rather than the earlier nine-TRS sibling diagnostics.
    animated_names = ["body_bob", "wheel_front", "wheel_rear"]
    missing_animated = [name for name in animated_names if name not in bone_names]
    if missing_animated:
        raise SystemExit(f"missing high-speed animation bones: {missing_animated}")

    # pycgfx globally re-sorts bones by translucent-material ratio, separating
    # the high-speed parent and its two axle meshes. HOME's banner player
    # expects the rigid CANM targets to occupy the same contiguous ordering as
    # the model skeleton. Restore that order and remap every numeric reference.
    desired_bone_order = [
        "Scene root",
        "COMMON",
        "name",
        "world",
        "vehicle_motion",
        "body_bob",
        "wheel_front",
        "wheel_rear",
        "native_car_shadow",
        "wheel_mesh",
    ]
    if set(bone_names) != set(desired_bone_order):
        raise SystemExit(
            "unexpected high-speed skeleton: " + ", ".join(bone_names)
        )
    old_index = {name: index for index, name in enumerate(bone_names)}
    bone_dict_nodes = model.skeleton.bones.dict.nodes
    bone_sentinel = bone_dict_nodes[0]
    node_by_name = {
        node.name: node for node in bone_dict_nodes[1:]
    }
    model.skeleton.bones.dict.nodes = [
        bone_sentinel,
        *(node_by_name[name] for name in desired_bone_order),
    ]
    model.skeleton.bones.dict.regenerate()
    new_index = {name: index for index, name in enumerate(desired_bone_order)}
    old_to_new = {
        old_index[name]: new_index[name] for name in desired_bone_order
    }
    for name in desired_bone_order:
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
    skeletal_group = model.animation_group_descriptions.dict[
        "SkeletalAnimation"
    ]
    group_dict_nodes = skeletal_group.members.dict.nodes
    group_sentinel = group_dict_nodes[0]
    group_node_by_name = {
        node.name: node for node in group_dict_nodes[1:]
    }
    skeletal_group.members.dict.nodes = [
        group_sentinel,
        *(group_node_by_name[name] for name in desired_bone_order),
    ]
    skeletal_group.members.dict.regenerate()
    # CTGP enables translated skeletal animation and uses Maya's child-scale
    # rule.  The latter is especially important here because the HOME-facing
    # ``world`` node carries the composition scale above both axle bones.
    model.skeleton.flags = SkeletonFlag.IsTranslateAnimationEnabled
    model.skeleton.scaling_rule = SkeletonScalingRule.Maya
    bone_names = list(model.skeleton.bones)
    allowed_bones = {
        "Scene root",
        "COMMON",
        "world",
        "name",
        "vehicle_motion",
        "body_bob",
        "wheel_front",
        "wheel_rear",
        "native_car_shadow",
        "wheel_mesh",
    }
    unexpected_bones = set(bone_names) - allowed_bones
    if unexpected_bones:
        raise SystemExit(
            "DFF hierarchy was not flattened: "
            + ", ".join(sorted(unexpected_bones))
        )
    logo_bone = model.skeleton.bones["name"]
    if tuple(logo_bone.position.values()) != (0, 0, 0):
        raise SystemExit("name must be identity-translated like ClouDS")
    for child_name in ("vehicle_motion", "native_car_shadow"):
        child = model.skeleton.bones[child_name]
        if child.parent_id != bone_names.index("world"):
            raise SystemExit(f"{child_name} must be a direct child of world")
    body_bob = model.skeleton.bones["body_bob"]
    if body_bob.parent_id != bone_names.index("vehicle_motion"):
        raise SystemExit("body_bob must be a child of vehicle_motion")
    for axle_name in ("wheel_front", "wheel_rear"):
        axle = model.skeleton.bones[axle_name]
        if axle.parent_id != bone_names.index("vehicle_motion"):
            raise SystemExit(f"{axle_name} must be a child of vehicle_motion")
        # pycgfx copies the glTF translation into Bone.position, but leaves the
        # serialized bind-pose local matrix at identity.  That is harmless for
        # rigid meshes; for a skinned axle HOME evaluates
        # local * animation * inverse_base, so identity here applies the
        # inverse axle translation without first restoring the axle pivot.  It
        # is exactly why the wheels jump away from the car on hardware.
        #
        # CTGP-7 stores the axle translation in both Bone.position and the
        # fourth component of each local-matrix row, keeps world at identity,
        # and uses the inverse translation in inverse_base.  Match that proven
        # bind pose and its flags byte-for-byte in meaning.
        x, y, z = axle.position.values()
        axle.local = Matrix(
            Vector4(1.0, 0.0, 0.0, x),
            Vector4(0.0, 1.0, 0.0, y),
            Vector4(0.0, 0.0, 1.0, z),
        )
        axle.flags = (
            BoneFlag.IsRotateZero
            | BoneFlag.IsScaleOne
            | BoneFlag.IsUniformScale
            | BoneFlag.IsSegmentScaleCompensate
            | BoneFlag.IsNeedRendering
            | BoneFlag.IsLocalMatrixCalculate
            | BoneFlag.IsWorldMatrixCalculate
        )
    wheel_mesh_bone = model.skeleton.bones["wheel_mesh"]
    if wheel_mesh_bone.parent_id != bone_names.index("vehicle_motion"):
        raise SystemExit("wheel_mesh must be a child of vehicle_motion")
    # ``world`` is the HOME-controlled rotating vehicle root.  ``name`` is its
    # sibling and must counter the viewing rotation so the flat logo remains
    # screen-facing instead of visibly turning with the car.
    model.skeleton.bones["name"].billboard_mode = BillboardMode.YAxial

    owners: list[str] = []
    for mesh in model.meshes.data.contents:
        shape = model.shapes.data.contents[mesh.shape_index]
        primitive_sets = shape.primitive_sets.data.contents
        if len(primitive_sets) != 1:
            raise SystemExit(
                f"{shape.name} has {len(primitive_sets)} primitive sets"
            )
        primitive_set = primitive_sets[0]
        related = list(primitive_set.related_bones.data.contents)
        related_names = [bone_names[index] for index in related]
        is_wheel_skin = set(related_names) == {
            "wheel_mesh",
            "wheel_front",
            "wheel_rear",
        }
        if is_wheel_skin:
            if primitive_set.skinning_mode != 2:
                raise SystemExit(
                    f"{shape.name} expected smooth skinning mode 2, got "
                    f"{primitive_set.skinning_mode}"
                )
            # pycgfx prepends the mesh node to skin.joints and increments every
            # JOINTS_0 index. CTGP-7 uses skinning mode 1 with only the two
            # actual wheel bones in related_bones. Remove that synthetic slot
            # and restore per-vertex indices 0=front, 1=rear.
            primitive_set.related_bones.data.contents = [
                bone_names.index("wheel_front"),
                bone_names.index("wheel_rear"),
            ]
            primitive_set.skinning_mode = 1
            owner = "wheel_mesh"
        else:
            if primitive_set.skinning_mode != 0 or len(related) != 1:
                raise SystemExit(
                    f"{shape.name} has unexpected rigid binding "
                    f"mode={primitive_set.skinning_mode}, bones={related_names}"
                )
            owner = related_names[0]
        mesh.mesh_node_name = owner
        owners.append(owner)

        saw_bone_index = False
        wheel_weight_attributes = []
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
                    if not is_wheel_skin:
                        raise SystemExit(
                            f"{shape.name} unexpectedly contains skin attributes"
                        )
                    if stream.usage == VertexAttributeUsage.BoneIndex:
                        saw_bone_index = True
                        if int(stream.format_type) != 5121:
                            raise SystemExit(
                                "wheel JOINTS_0 must remain unsigned-byte"
                            )
                        values = bytearray(stream.vertex_stream_data)
                        if not values or min(values) < 1 or max(values) > 2:
                            raise SystemExit(
                                "wheel JOINTS_0 is outside pycgfx's 1..2 range"
                            )
                        if stream.components_count != 4 or len(values) % 4:
                            raise SystemExit(
                                "wheel JOINTS_0 must enter as glTF VEC4"
                            )
                        joints = []
                        for offset in range(0, len(values), 4):
                            joint, *unused = values[offset : offset + 4]
                            if any(value != 1 for value in unused):
                                raise SystemExit(
                                    "wheel JOINTS_0 has more than one active bone"
                                )
                            joints.append(joint - 1)
                        # CTGP's skinning_mode=1 is rigid-per-vertex skinning:
                        # BoneIndex is one unsigned byte, and its weight is
                        # implicit.  Leaving glTF's four indices here makes
                        # HOME configure the wrong attribute stride; the
                        # resulting vertices orbit away from the car and the
                        # wheel surface itself never receives the axle spin.
                        stream.vertex_stream_data = bytes(joints)
                        stream.components_count = 1
                    else:
                        wheel_weight_attributes.append(attribute)
        if is_wheel_skin:
            if not saw_bone_index or not wheel_weight_attributes:
                raise SystemExit("wheel mesh is missing input skin attributes")
            # CTGP has no BoneWeight stream in mode 1.  A single BoneIndex
            # selects one related bone at full weight.
            shape.vertex_attributes.data.contents = [
                attribute
                for attribute in shape.vertex_attributes.data.contents
                if attribute not in wheel_weight_attributes
            ]
            wheel_attributes = shape.vertex_attributes.data.contents
            expected_usages = [
                VertexAttributeUsage.Position,
                VertexAttributeUsage.Normal,
                VertexAttributeUsage.TextureCoordinate0,
                VertexAttributeUsage.BoneIndex,
            ]
            if [attribute.usage for attribute in wheel_attributes] != expected_usages:
                raise SystemExit(
                    "wheel attributes do not match CTGP position/normal/UV/bone layout"
                )
            component_sizes = {0x1401: 1, 0x1406: 4}
            element_sizes = []
            vertex_counts = []
            for attribute in wheel_attributes:
                component_size = component_sizes.get(int(attribute.format_type))
                if component_size is None:
                    raise SystemExit("unsupported wheel vertex component format")
                element_size = component_size * attribute.components_count
                element_sizes.append(element_size)
                if len(attribute.vertex_stream_data) % element_size:
                    raise SystemExit("misaligned wheel vertex stream")
                vertex_counts.append(len(attribute.vertex_stream_data) // element_size)
            if len(set(vertex_counts)) != 1:
                raise SystemExit(f"wheel vertex counts disagree: {vertex_counts}")

            # Nintendo's exporter stores this mode-1 layout in one 36-byte
            # interleaved stream: float3 position, float3 normal, float2 UV,
            # one UByte bone index, then three bytes of alignment padding.
            # Match it exactly instead of relying on pycgfx's four independent
            # vertex buffers and the GPU attribute command they imply.
            offsets = (0, 12, 24, 32)
            stride = 36
            interleaved_data = bytearray()
            for vertex_index in range(vertex_counts[0]):
                row = bytearray(stride)
                for attribute, element_size, offset in zip(
                    wheel_attributes, element_sizes, offsets
                ):
                    start = vertex_index * element_size
                    row[offset : offset + element_size] = (
                        attribute.vertex_stream_data[start : start + element_size]
                    )
                    attribute.vert_offset = offset
                interleaved_data.extend(row)
            for attribute in wheel_attributes:
                attribute.vertex_stream_data = b""
            interleaved = InterleavedVertexStream()
            interleaved.usage = VertexAttributeUsage.Interlave
            interleaved.vertex_stream_data = bytes(interleaved_data)
            interleaved.vertex_data_entry_size = stride
            interleaved.vertex_streams.data.contents = wheel_attributes
            shape.vertex_attributes.data.contents = [interleaved]
            primitive_set.primitives.data.contents[0].flags = 0x8

    if owners.count("body_bob") < 1:
        raise SystemExit("body_bob has no visible body SOBJ")
    if owners.count("wheel_mesh") != 1:
        raise SystemExit(
            f"wheel_mesh must be one skinned SOBJ, got {owners.count('wheel_mesh')}"
        )
    for axle_name in ("wheel_front", "wheel_rear"):
        if owners.count(axle_name) != 0:
            raise SystemExit(f"{axle_name} unexpectedly owns a direct SOBJ")
    unexpected_owners = set(owners) - {
        "vehicle_motion",
        "body_bob",
        "native_car_shadow",
        "name",
        "wheel_mesh",
        *animated_names,
    }
    if unexpected_owners:
        raise SystemExit(
            "unexpected rigid SOBJ owners: "
            + ", ".join(sorted(unexpected_owners))
        )
    if owners.count("name") != 1:
        raise SystemExit(
            f"expected one logo SOBJ bound to name, got {owners.count('name')}"
        )
    if not all(mesh.mesh_node_name for mesh in model.meshes.data.contents):
        raise SystemExit("one or more SOBJ mesh-node bindings are empty")

    # Official CGFX keeps the visible skinned wheel object as a MeshNode, not
    # as a third bone beneath the animated vehicle.  Keeping pycgfx's synthetic
    # wheel_mesh bone makes HOME apply the vehicle transform once through that
    # node and again through the two skin bones.  Remove it from both matching
    # bone dictionaries and register every visible owner in CMDL.mesh_nodes so
    # SOBJ visibility indices are real rather than pycgfx's 0xffff sentinel.
    synthetic_wheel_bone = model.skeleton.bones["wheel_mesh"]
    for name in list(model.skeleton.bones):
        if name == "wheel_mesh":
            continue
        bone = model.skeleton.bones[name]
        if bone.child is synthetic_wheel_bone:
            bone.child = synthetic_wheel_bone.next_sibling
        if bone.previous_sibling is synthetic_wheel_bone:
            bone.previous_sibling = synthetic_wheel_bone.previous_sibling
        if bone.next_sibling is synthetic_wheel_bone:
            bone.next_sibling = synthetic_wheel_bone.next_sibling
    model.skeleton.bones.dict.nodes = [
        node
        for node in model.skeleton.bones.dict.nodes
        if node is bone_sentinel or node.name != "wheel_mesh"
    ]
    model.skeleton.bones.dict.regenerate()
    skeletal_group.members.dict.nodes = [
        node
        for node in skeletal_group.members.dict.nodes
        if node is group_sentinel or node.name != "wheel_mesh"
    ]
    skeletal_group.members.dict.regenerate()
    bone_names = list(model.skeleton.bones)
    if "wheel_mesh" in bone_names:
        raise SystemExit("synthetic wheel_mesh bone survived removal")

    unique_mesh_nodes = []
    for owner in owners:
        if owner not in unique_mesh_nodes:
            unique_mesh_nodes.append(owner)
    for owner in unique_mesh_nodes:
        model.mesh_nodes.add(owner, MeshNodeVisibility(owner))
    for mesh in model.meshes.data.contents:
        mesh.mesh_node_visibility_index = model.mesh_nodes.get_index(
            mesh.mesh_node_name
        )

    # pycgfx's generic PBR conversion creates a strong white specular stage.
    # The detailed banner tutorial's proven correction keeps diffuse form but
    # removes the washout-prone specular term for HOME Menu lighting.
    matte_materials: list[str] = []
    for material_name in model.materials:
        material = model.materials[material_name]
        material.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
        specular_stage = material.fragment_shader.texture_combiners[2]
        specular_stage.src_rgb = 0xFFF
        specular_stage.combine_rgb = 0
        material.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
        material.fragment_shader.fragment_lighting_table.distribution_0_sampler = None
        if material_name == "banner_logo_alpha":
            # A flat screen logo must retain its authored white/pink colours
            # instead of dimming with the 3D vehicle.  Pass the textured first
            # stage through both lighting/specular stages and disable fragment
            # lighting only for this material.
            for stage in material.fragment_shader.texture_combiners[1:3]:
                stage.src_rgb = 0xFFF
                stage.combine_rgb = 0
            material.flags = MTOBFlag(0)
        matte_materials.append(material_name)

    animation_names = list(cgfx.data.skeletal_animations)
    if animation_names != ["COMMON"]:
        raise SystemExit(
            f"expected only skeletal animation COMMON, got {animation_names}"
    )
    animation = cgfx.data.skeletal_animations["COMMON"]
    member_nodes = animation.member_animations_data.dict.nodes
    member_sentinel = member_nodes[0]
    member_by_name = {node.name: node for node in member_nodes[1:]}
    if set(member_by_name) != set(animated_names):
        raise SystemExit(
            "unexpected high-speed CANM members: "
            + ", ".join(member_by_name)
        )
    animation.member_animations_data.dict.nodes = [
        member_sentinel,
        *(member_by_name[name] for name in animated_names),
    ]
    animation.member_animations_data.dict.regenerate()
    member_names = list(animation.member_animations_data)
    expected_members = animated_names
    if member_names != expected_members:
        raise SystemExit(
            "CANM member layout differs from ClouDS-style three-rigid-node "
            f"dictionary: {member_names}"
        )
    member_bone_indices = [bone_names.index(name) for name in member_names]
    if member_bone_indices != list(
        range(member_bone_indices[0], member_bone_indices[0] + len(member_names))
    ):
        raise SystemExit(
            f"animated motion bones are not contiguous: {member_bone_indices}"
        )
    for member_name in member_names:
        member = animation.member_animations_data[member_name]
        if (
            not isinstance(member, CANMBoneTransform)
            or member.primitive_type != PrimitiveType.Transform
        ):
            raise SystemExit(
                f"{member_name} is not ordinary rigid Transform (5)"
            )

    # The installed CTGP-7 banner is the authoritative vehicle-animation
    # reference: 360 ticks, body position-Y only, and local rotation-X only on
    # each child axle.  Every other transform component is ignored.  Author
    # those sparse curves directly so Blender/pycgfx cannot expand them back
    # into the nine-channel layout that HOME rejected in earlier builds.
    def make_curve(keys, interpolation, quantization):
        curve = FloatAnimationCurve()
        curve.start_frame = 0.0
        curve.end_frame = 360.0
        segment = FloatSegment()
        segment.start_frame = 0.0
        segment.end_frame = 360.0
        segment.interpolation = interpolation
        segment.quantization = quantization
        segment.keys = keys
        curve.segments = [segment]
        return curve

    motion = animation.member_animations_data["body_bob"]
    front = animation.member_animations_data["wheel_front"]
    rear = animation.member_animations_data["wheel_rear"]
    for member in (motion, front, rear):
        for field in (
            "scale_x",
            "scale_y",
            "scale_z",
            "rot_x",
            "rot_y",
            "rot_z",
            "pos_x",
            "pos_y",
            "pos_z",
        ):
            setattr(member, field, None)

    bob_keys = []
    for key_index in range(len(ROAD_BOB_OFFSETS) + 1):
        frame = key_index * 10
        profile_index = key_index % len(ROAD_BOB_OFFSETS)
        value = ROAD_BOB_BASE + ROAD_BOB_OFFSETS[profile_index]
        previous_value = ROAD_BOB_OFFSETS[
            (profile_index - 1) % len(ROAD_BOB_OFFSETS)
        ]
        next_value = ROAD_BOB_OFFSETS[
            (profile_index + 1) % len(ROAD_BOB_OFFSETS)
        ]
        slope = (next_value - previous_value) / 20.0
        bob_keys.append(Hermite128Key(float(frame), value, slope, slope))
    motion.pos_y = make_curve(
        bob_keys,
        InterpolationType.CubicSpline,
        QuantizationType.Hermite128,
    )

    roll_keys = []
    for key_index in range(len(ROAD_ROLL_DEGREES) + 1):
        frame = key_index * 10
        profile_index = key_index % len(ROAD_ROLL_DEGREES)
        value = math.radians(ROAD_ROLL_DEGREES[profile_index])
        roll_keys.append(StepLinear64Key(float(frame), value))
    # Blender Y (vehicle longitudinal roll axis) maps to CGFX rotation Z.
    motion.rot_z = make_curve(
        roll_keys,
        InterpolationType.Linear,
        QuantizationType.StepLinear64,
    )

    wheel_curve_values = (
        StepLinear64Key(0.0, math.tau * WHEEL_ROTATIONS_PER_LOOP / 2.0),
        StepLinear64Key(360.0, -math.tau * WHEEL_ROTATIONS_PER_LOOP / 2.0),
    )
    front.rot_x = make_curve(
        list(wheel_curve_values),
        InterpolationType.Linear,
        QuantizationType.StepLinear64,
    )
    rear.rot_x = make_curve(
        [
            StepLinear64Key(key.frame, key.value)
            for key in wheel_curve_values
        ],
        InterpolationType.Linear,
        QuantizationType.StepLinear64,
    )
    animation.frame_size = 360.0

    if (
        motion.pos_y is None
        or motion.rot_z is None
        or len(motion.pos_y.segments[0].keys) != 37
        or len(motion.rot_z.segments[0].keys) != 37
    ):
        raise SystemExit("body_bob does not have 37-key bob/roll curves")
    roll_values = [key.value for key in motion.rot_z.segments[0].keys]
    if not (
        min(roll_values) <= math.radians(-3.0)
        and max(roll_values) >= math.radians(3.5)
    ):
        raise SystemExit("body_bob roll range is not visibly asymmetric")
    for axle_name, axle in (("wheel_front", front), ("wheel_rear", rear)):
        if axle.rot_x is None or len(axle.rot_x.segments[0].keys) != 2:
            raise SystemExit(f"{axle_name} does not have the CTGP-7 wheel curve")
        wheel_values = [key.value for key in axle.rot_x.segments[0].keys]
        if not (wheel_values[0] > 0.0 and wheel_values[-1] < 0.0):
            raise SystemExit(f"{axle_name} rotation direction was not reversed")

    payload = bytearray(pycgfx.write(cgfx))
    # Nintendo's CTGP-7 sparse Transform members all set the otherwise
    # undocumented 0x400000 mode bit.  pycgfx emits zero for that bit because
    # its TransformFlag enum does not name it.  Full-nine-channel ClouDS
    # members do not need the bit, but CTGP's body-Y-only and wheel-X-only
    # members do.  Patch the three serialized flag words after layout is final.
    sparse_transform_mode = 0x400000
    for member_name in member_names:
        member = animation.member_animations_data[member_name]
        flags = struct.unpack_from("<I", payload, member.offset)[0]
        struct.pack_into(
            "<I",
            payload,
            member.offset,
            flags | sparse_transform_mode,
        )
    payload = bytes(payload)
    if len(payload) >= SIZE_LIMIT:
        raise SystemExit(
            f"CGFX is {len(payload)} bytes; limit is below {SIZE_LIMIT}"
        )
    output_path = args.output_cgfx.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)

    owner_counts = Counter(owners)
    print(
        f"wrote {output_path} ({len(payload)} bytes); "
        f"SOBJ bindings={len(owners)}, high-speed members="
        f"{len(animated_names)}; matte materials="
        f"{len(matte_materials)}; rigid Transform (5) validated"
    )


if __name__ == "__main__":
    main()
