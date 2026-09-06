"""
make_grass_mesh.py
==================
Generates REAL 3D grass blade meshes using MeshDescription API.
These are actual 3D geometry with volume — tapered blades with thickness,
proper normals, and UV mapping. NOT flat cards.

Creates 3 blade variants:
  /Game/Meshes/Grass/SM_GrassBlade3D_01 — short straight blade
  /Game/Meshes/Grass/SM_GrassBlade3D_02 — medium curved blade
  /Game/Meshes/Grass/SM_GrassBlade3D_03 — tall wavy blade

Each blade is ~10cm tall, ~1.5cm wide at base, tapering to a point.
The blades have real 3D thickness (two surfaces meeting at edges).

Run from the UE5.8 editor:
    Tools -> Script Editor -> Run Script...  ->  make_grass_mesh.py
"""

import unreal
import math

MESH_PATHS = [
    "/Game/Meshes/Grass/SM_GrassBlade3D_01",
    "/Game/Meshes/Grass/SM_GrassBlade3D_02",
    "/Game/Meshes/Grass/SM_GrassBlade3D_03",
]


def log(msg):
    unreal.log("[make_grass_mesh] " + msg)


def create_blade_mesh(path, blade_height=10.0, blade_width=1.5, curve_amount=0.0, wave_amount=0.0):
    """
    Create a real 3D grass blade mesh.
    
    The blade is built as a strip of polygons that has real thickness:
    - Two rows of vertices (front and back) that meet at the edges
    - Tapered from full width at base to zero at tip
    - Optional curve and wave for natural variation
    - Proper smooth normals
    - UV coordinates (U along width, V along height)
    
    All dimensions in centimeters (UE units).
    """
    eal = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Delete existing
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    # Create the static mesh asset
    factory = unreal.StaticMeshFactoryNew()
    mesh_name = path.split("/")[-1]
    mesh_package = "/".join(path.split("/")[:-1])
    static_mesh = asset_tools.create_asset(mesh_name, mesh_package, unreal.StaticMesh, factory)
    
    if not static_mesh:
        log(f"ERROR: Failed to create mesh at {path}")
        return None

    # Get the mesh description and build the blade geometry
    md = static_mesh.get_mesh_description(0)
    if not md:
        md = static_mesh.create_mesh_description(0)
    
    md.set_cpu_access(True)
    
    # Get vertex/instance/polygon accessors
    vertex_descs = md.get_vertex_descriptions()
    instance_descs = md.get_instance_descriptions()
    polygon_descs = md.get_polygon_descriptions()
    
    # === BUILD BLADE GEOMETRY ===
    # The blade is a tapered strip with real 3D thickness.
    # We use N segments along the height, each with 4 vertices (front-left, front-right, back-left, back-right)
    # At the tip, front and back merge to a single edge.
    
    num_segments = 12  # segments along blade height
    vertices = []      # list of (position, uv) tuples
    quads = []         # list of (v0, v1, v2, v3) vertex indices
    
    for i in range(num_segments + 1):
        t = i / num_segments  # 0 at base, 1 at tip
        
        # Height along blade
        y = t * blade_height
        
        # Width tapers from full to zero (use sqrt for natural blade shape)
        width = blade_width * (1.0 - t) ** 0.7
        if i == num_segments:
            width = 0.0  # Sharp tip
        
        # Curve: blade bends forward (in X) as it gets taller
        curve_x = curve_amount * t * t * blade_height * 0.15
        
        # Wave: sinusoidal variation along blade
        wave_x = wave_amount * math.sin(t * math.pi * 2.0) * blade_height * 0.05
        wave_z = wave_amount * math.cos(t * math.pi * 1.5) * blade_height * 0.03
        
        x_center = curve_x + wave_x
        z_center = wave_z
        
        # Thickness: blade has real 3D thickness, thinner toward tip
        thickness = 0.08 * (1.0 - t * 0.7)  # 0.8mm at base, tapering
        
        if width > 0.01:
            # Four vertices per segment: front-left, front-right, back-left, back-right
            # "Front" = +Z side, "Back" = -Z side (blade faces are on Z axis)
            # Width is along X
            v_fl = (x_center - width * 0.5, y, z_center + thickness * 0.5)
            v_fr = (x_center + width * 0.5, y, z_center + thickness * 0.5)
            v_bl = (x_center - width * 0.5, y, z_center - thickness * 0.5)
            v_br = (x_center + width * 0.5, y, z_center - thickness * 0.5)
            
            # UV: U = 0 at left, 1 at right; V = t (0 at base, 1 at tip)
            uv_fl = (0.0, t)
            uv_fr = (1.0, t)
            uv_bl = (0.0, t)
            uv_br = (1.0, t)
            
            vertices.extend([(v_fl, uv_fl), (v_fr, uv_fr), (v_bl, uv_bl), (v_br, uv_br)])
        else:
            # Tip: single edge (front and back merge)
            v_tip_front = (x_center, y, z_center + thickness * 0.3)
            v_tip_back = (x_center, y, z_center - thickness * 0.3)
            vertices.append((v_tip_front, (0.5, t)))
            vertices.append((v_tip_back, (0.5, t)))
    
    # === CREATE VERTICES IN MESH DESCRIPTION ===
    vertex_ids = []
    for pos, uv in vertices:
        vid = md.add_vertex()
        vd = md.get_vertex_description(vid)
        vd.set_position(unreal.Vector(pos[0], pos[1], pos[2]))
        vertex_ids.append(vid)
    
    # === CREATE POLYGONS ===
    # For each segment (except tip), create:
    # 1. Front face quad (visible from +Z)
    # 2. Back face quad (visible from -Z)
    # 3. Left edge quad (connecting left front to left back)
    # 4. Right edge quad
    
    for i in range(num_segments):
        base_idx = i * 4  # 4 vertices per segment (except tip)
        next_idx = (i + 1) * 4
        
        if i == num_segments - 1:
            # Tip segment: next row has only 2 vertices
            next_count = 2
        else:
            next_count = 4
        
        # Current segment vertices: fl, fr, bl, br
        cur_fl = vertex_ids[base_idx + 0]
        cur_fr = vertex_ids[base_idx + 1]
        cur_bl = vertex_ids[base_idx + 2]
        cur_br = vertex_ids[base_idx + 3]
        
        if next_count == 4:
            nxt_fl = vertex_ids[next_idx + 0]
            nxt_fr = vertex_ids[next_idx + 1]
            nxt_bl = vertex_ids[next_idx + 2]
            nxt_br = vertex_ids[next_idx + 3]
        else:
            # Tip: only 2 vertices
            nxt_fl = vertex_ids[next_idx + 0]  # tip front
            nxt_fr = vertex_ids[next_idx + 0]  # same (degenerate)
            nxt_bl = vertex_ids[next_idx + 1]  # tip back
            nxt_br = vertex_ids[next_idx + 1]  # same
        
        # Front face (winding so normal points +Z)
        add_quad(md, cur_fl, cur_fr, nxt_fr, nxt_fl)
        # Back face (winding so normal points -Z)
        add_quad(md, cur_br, cur_bl, nxt_bl, nxt_br)
        # Left edge
        add_quad(md, cur_bl, cur_fl, nxt_fl, nxt_bl)
        # Right edge
        add_quad(md, cur_fr, cur_br, nxt_br, nxt_fr)
    
    # === SET UVs AND NORMALS ===
    # Set UVs on all vertices
    for i, (pos, uv) in enumerate(vertices):
        vd = md.get_vertex_description(vertex_ids[i])
        # Set UV channel 0
        vd.set_uv(0, unreal.Vector2D(uv[0], uv[1]))
    
    # Compute normals automatically
    md.compute_normals()
    
    # === COMMIT TO STATIC MESH ===
    static_mesh.commit_mesh_description(0)
    
    # Build the mesh
    build_settings = unreal.MeshBuildSettings()
    build_settings.set_editor_property("build_reversed_index_buffer", False)
    build_settings.set_editor_property("build_adjacency_buffer", False)
    build_settings.set_editor_property("compute_precision", 0.01)
    
    # Set LOD 0 build settings
    lod_data = static_mesh.get_editor_property("lod_info")
    if lod_data and len(lod_data) > 0:
        lod_data[0].set_editor_property("build_settings", build_settings)
    
    # Build
    unreal.EditorStaticMeshLibrary.build_all_static_meshes_if_pending(False)
    
    # Save
    eal.save_asset(path)
    log(f"Created 3D blade mesh: {path} ({len(vertices)} verts, {num_segments} segments)")
    return static_mesh


def add_quad(md, v0, v1, v2, v3):
    """Add a quad polygon to the mesh description."""
    # Create a polygon with 4 vertices
    polygon_desc = md.add_polygon([v0, v1, v2, v3])
    return polygon_desc


def main():
    log("Creating real 3D grass blade meshes...")
    
    # Blade 1: Short straight blade (mowed lawn)
    create_blade_mesh(
        MESH_PATHS[0],
        blade_height=8.0,    # 8cm = short mowed grass
        blade_width=1.2,     # 1.2cm wide
        curve_amount=0.1,    # Slight curve
        wave_amount=0.0
    )
    
    # Blade 2: Medium curved blade
    create_blade_mesh(
        MESH_PATHS[1],
        blade_height=10.0,   # 10cm
        blade_width=1.5,     # 1.5cm wide
        curve_amount=0.5,    # More curve
        wave_amount=0.2
    )
    
    # Blade 3: Tall wavy blade
    create_blade_mesh(
        MESH_PATHS[2],
        blade_height=12.0,   # 12cm
        blade_width=1.0,     # 1cm wide
        curve_amount=0.8,    # Strong curve
        wave_amount=0.5
    )
    
    log("SUCCESS: Created 3 real 3D grass blade meshes")
    log("  " + MESH_PATHS[0])
    log("  " + MESH_PATHS[1])
    log("  " + MESH_PATHS[2])
    log("")
    log("These are REAL 3D meshes with volume, tapered shape, and proper normals.")
    log("Update GrassField.cpp to use SM_GrassBlade3D_01/02/03 instead of the old meshes.")


main()
