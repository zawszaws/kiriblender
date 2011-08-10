# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8-80 compliant>

__all__ = (
    "mesh_linked_faces",
    "edge_face_count_dict",
    "edge_face_count",
    "edge_loops_from_faces",
    "edge_loops_from_edges",
    "ngon_tesselate",
    "face_random_points",
    )


def mesh_linked_faces(mesh):
    """
    Splits the mesh into connected faces, use this for seperating cubes from
    other mesh elements within 1 mesh datablock.

    :arg mesh: the mesh used to group with.
    :type mesh: :class:`Mesh`
    :return: lists of lists containing faces.
    :rtype: list
    """

    # Build vert face connectivity
    vert_faces = [[] for i in range(len(mesh.vertices))]
    for f in mesh.faces:
        for v in f.vertices:
            vert_faces[v].append(f)

    # sort faces into connectivity groups
    face_groups = [[f] for f in mesh.faces]
    face_mapping = list(range(len(mesh.faces)))  # map old, new face location

    # Now clump faces iterativly
    ok = True
    while ok:
        ok = False

        for i, f in enumerate(mesh.faces):
            mapped_index = face_mapping[f.index]
            mapped_group = face_groups[mapped_index]

            for v in f.vertices:
                for nxt_f in vert_faces[v]:
                    if nxt_f != f:
                        nxt_mapped_index = face_mapping[nxt_f.index]

                        # We are not a part of the same group
                        if mapped_index != nxt_mapped_index:
                            ok = True

                            # Assign mapping to this group so they
                            # all map to this group
                            for grp_f in face_groups[nxt_mapped_index]:
                                face_mapping[grp_f.index] = mapped_index

                            # Move faces into this group
                            mapped_group.extend(face_groups[nxt_mapped_index])

                            # remove reference to the list
                            face_groups[nxt_mapped_index] = None

    # return all face groups that are not null
    # this is all the faces that are connected in their own lists.
    return [fg for fg in face_groups if fg]


def edge_face_count_dict(mesh):
    """
    :return: dict of edge keys with their value set to the number of
       faces using each edge.
    :rtype: dict
    """
    face_edge_keys = [face.edge_keys for face in mesh.faces]
    face_edge_count = {}
    for face_keys in face_edge_keys:
        for key in face_keys:
            try:
                face_edge_count[key] += 1
            except:
                face_edge_count[key] = 1

    return face_edge_count


def edge_face_count(mesh):
    """
    :return: list face users for each item in mesh.edges.
    :rtype: list
    """
    edge_face_count = edge_face_count_dict(mesh)
    get = dict.get
    return [get(edge_face_count, ed.key, 0) for ed in mesh.edges]


def edge_loops_from_faces(mesh, faces=None, seams=()):
    """
    Edge loops defined by faces

    Takes me.faces or a list of faces and returns the edge loops
    These edge loops are the edges that sit between quads, so they dont touch
    1 quad, note: not connected will make 2 edge loops,
    both only containing 2 edges.

    return a list of edge key lists
    [[(0, 1), (4, 8), (3, 8)], ...]

    :arg mesh: the mesh used to get edge loops from.
    :type mesh: :class:`Mesh`
    :arg faces: optional face list to only use some of the meshes faces.
    :type faces: :class:`MeshFaces`, sequence or or NoneType
    :return: return a list of edge vertex index lists.
    :rtype: list
    """

    OTHER_INDEX = 2, 3, 0, 1  # opposite face index

    if faces is None:
        faces = mesh.faces

    edges = {}

    for f in faces:
#        if len(f) == 4:
        if f.vertices_raw[3] != 0:
            edge_keys = f.edge_keys
            for i, edkey in enumerate(f.edge_keys):
                edges.setdefault(edkey, []).append(edge_keys[OTHER_INDEX[i]])

    for edkey in seams:
        edges[edkey] = []

    # Collect edge loops here
    edge_loops = []

    for edkey, ed_adj in edges.items():
        if 0 < len(ed_adj) < 3:  # 1 or 2
            # Seek the first edge
            context_loop = [edkey, ed_adj[0]]
            edge_loops.append(context_loop)
            if len(ed_adj) == 2:
                other_dir = ed_adj[1]
            else:
                other_dir = None

            ed_adj[:] = []

            flipped = False

            while 1:
                # from knowing the last 2, look for th next.
                ed_adj = edges[context_loop[-1]]
                if len(ed_adj) != 2:
                    # the original edge had 2 other edges
                    if other_dir and flipped == False:
                        flipped = True  # only flip the list once
                        context_loop.reverse()
                        ed_adj[:] = []
                        context_loop.append(other_dir)  # save 1 lookiup

                        ed_adj = edges[context_loop[-1]]
                        if len(ed_adj) != 2:
                            ed_adj[:] = []
                            break
                    else:
                        ed_adj[:] = []
                        break

                i = ed_adj.index(context_loop[-2])
                context_loop.append(ed_adj[not  i])

                # Dont look at this again
                ed_adj[:] = []

    return edge_loops


def edge_loops_from_edges(mesh, edges=None):
    """
    Edge loops defined by edges

    Takes me.edges or a list of edges and returns the edge loops

    return a list of vertex indices.
    [ [1, 6, 7, 2], ...]

    closed loops have matching start and end values.
    """
    line_polys = []

    # Get edges not used by a face
    if edges is None:
        edges = mesh.edges

    if not hasattr(edges, "pop"):
        edges = edges[:]

    while edges:
        current_edge = edges.pop()
        vert_end, vert_start = current_edge.vertices[:]
        line_poly = [vert_start, vert_end]

        ok = True
        while ok:
            ok = False
            #for i, ed in enumerate(edges):
            i = len(edges)
            while i:
                i -= 1
                ed = edges[i]
                v1, v2 = ed.vertices
                if v1 == vert_end:
                    line_poly.append(v2)
                    vert_end = line_poly[-1]
                    ok = 1
                    del edges[i]
                    # break
                elif v2 == vert_end:
                    line_poly.append(v1)
                    vert_end = line_poly[-1]
                    ok = 1
                    del edges[i]
                    #break
                elif v1 == vert_start:
                    line_poly.insert(0, v2)
                    vert_start = line_poly[0]
                    ok = 1
                    del edges[i]
                    # break
                elif v2 == vert_start:
                    line_poly.insert(0, v1)
                    vert_start = line_poly[0]
                    ok = 1
                    del edges[i]
                    #break
        line_polys.append(line_poly)

    return line_polys


def ngon_tesselate(from_data, indices, fix_loops=True):
    '''
    Takes a polyline of indices (fgon) and returns a list of face
    indicie lists. Designed to be used for importers that need indices for an
    fgon to create from existing verts.

    from_data: either a mesh, or a list/tuple of vectors.
    indices: a list of indices to use this list is the ordered closed polyline
       to fill, and can be a subset of the data given.
    fix_loops: If this is enabled polylines that use loops to make multiple
       polylines are delt with correctly.
    '''

    from mathutils.geometry import tesselate_polygon
    from mathutils import Vector
    vector_to_tuple = Vector.to_tuple

    if not indices:
        return []

    def mlen(co):
        # manhatten length of a vector, faster then length
        return abs(co[0]) + abs(co[1]) + abs(co[2])

    def vert_treplet(v, i):
        return v, vector_to_tuple(v, 6), i, mlen(v)

    def ed_key_mlen(v1, v2):
        if v1[3] > v2[3]:
            return v2[1], v1[1]
        else:
            return v1[1], v2[1]

    if not fix_loops:
        '''
        Normal single concave loop filling
        '''
        if type(from_data) in {tuple, list}:
            verts = [Vector(from_data[i]) for ii, i in enumerate(indices)]
        else:
            verts = [from_data.vertices[i].co for ii, i in enumerate(indices)]

        # same as reversed(range(1, len(verts))):
        for i in range(len(verts) - 1, 0, -1):
            if verts[i][1] == verts[i - 1][0]:
                verts.pop(i - 1)

        fill = tesselate_polygon([verts])

    else:
        '''
        Seperate this loop into multiple loops be finding edges that are
        used twice. This is used by lightwave LWO files a lot
        '''

        if type(from_data) in {tuple, list}:
            verts = [vert_treplet(Vector(from_data[i]), ii)
                     for ii, i in enumerate(indices)]
        else:
            verts = [vert_treplet(from_data.vertices[i].co, ii)
                     for ii, i in enumerate(indices)]

        edges = [(i, i - 1) for i in range(len(verts))]
        if edges:
            edges[0] = (0, len(verts) - 1)

        if not verts:
            return []

        edges_used = set()
        edges_doubles = set()
        # We need to check if any edges are used twice location based.
        for ed in edges:
            edkey = ed_key_mlen(verts[ed[0]], verts[ed[1]])
            if edkey in edges_used:
                edges_doubles.add(edkey)
            else:
                edges_used.add(edkey)

        # Store a list of unconnected loop segments split by double edges.
        # will join later
        loop_segments = []

        v_prev = verts[0]
        context_loop = [v_prev]
        loop_segments = [context_loop]

        for v in verts:
            if v != v_prev:
                # Are we crossing an edge we removed?
                if ed_key_mlen(v, v_prev) in edges_doubles:
                    context_loop = [v]
                    loop_segments.append(context_loop)
                else:
                    if context_loop and context_loop[-1][1] == v[1]:
                        #raise "as"
                        pass
                    else:
                        context_loop.append(v)

                v_prev = v
        # Now join loop segments

        def join_seg(s1, s2):
            if s2[-1][1] == s1[0][1]:
                s1, s2 = s2, s1
            elif s1[-1][1] == s2[0][1]:
                pass
            else:
                return False

            # If were stuill here s1 and s2 are 2 segments in the same polyline
            s1.pop()  # remove the last vert from s1
            s1.extend(s2)  # add segment 2 to segment 1

            if s1[0][1] == s1[-1][1]:  # remove endpoints double
                s1.pop()

            s2[:] = []  # Empty this segment s2 so we dont use it again.
            return True

        joining_segments = True
        while joining_segments:
            joining_segments = False
            segcount = len(loop_segments)

            for j in range(segcount - 1, -1, -1):  # reversed(range(segcount)):
                seg_j = loop_segments[j]
                if seg_j:
                    for k in range(j - 1, -1, -1):  # reversed(range(j)):
                        if not seg_j:
                            break
                        seg_k = loop_segments[k]

                        if seg_k and join_seg(seg_j, seg_k):
                            joining_segments = True

        loop_list = loop_segments

        for verts in loop_list:
            while verts and verts[0][1] == verts[-1][1]:
                verts.pop()

        loop_list = [verts for verts in loop_list if len(verts) > 2]
        # DONE DEALING WITH LOOP FIXING

        # vert mapping
        vert_map = [None] * len(indices)
        ii = 0
        for verts in loop_list:
            if len(verts) > 2:
                for i, vert in enumerate(verts):
                    vert_map[i + ii] = vert[2]
                ii += len(verts)

        fill = tesselate_polygon([[v[0] for v in loop] for loop in loop_list])
        #draw_loops(loop_list)
        #raise 'done loop'
        # map to original indices
        fill = [[vert_map[i] for i in reversed(f)] for f in fill]

    if not fill:
        print('Warning Cannot scanfill, fallback on a triangle fan.')
        fill = [[0, i - 1, i] for i in range(2, len(indices))]
    else:
        # Use real scanfill.
        # See if its flipped the wrong way.
        flip = None
        for fi in fill:
            if flip != None:
                break
            for i, vi in enumerate(fi):
                if vi == 0 and fi[i - 1] == 1:
                    flip = False
                    break
                elif vi == 1 and fi[i - 1] == 0:
                    flip = True
                    break

        if not flip:
            for i, fi in enumerate(fill):
                fill[i] = tuple([ii for ii in reversed(fi)])

    return fill


def face_random_points(num_points, faces):
    """
    Generates a list of random points over mesh faces.

    :arg num_points: the number of random points to generate on each face.
    :type int:
    :arg faces: list of the faces to generate points on.
    :type faces: :class:`MeshFaces`, sequence
    :return: list of random points over all faces.
    :rtype: list
    """

    from random import random
    from mathutils.geometry import area_tri

    # Split all quads into 2 tris, tris remain unchanged
    tri_faces = []
    for f in faces:
        tris = []
        verts = f.id_data.vertices
        fv = f.vertices[:]
        tris.append((verts[fv[0]].co,
                     verts[fv[1]].co,
                     verts[fv[2]].co,
                    ))
        if len(fv) == 4:
            tris.append((verts[fv[0]].co,
                         verts[fv[3]].co,
                         verts[fv[2]].co,
                        ))
        tri_faces.append(tris)

    # For each face, generate the required number of random points
    sampled_points = [None] * (num_points * len(faces))
    for i, tf in enumerate(tri_faces):
        for k in range(num_points):
            # If this is a quad, we need to weight its 2 tris by their area
            if len(tf) != 1:
                area1 = area_tri(*tf[0])
                area2 = area_tri(*tf[1])
                area_tot = area1 + area2

                area1 = area1 / area_tot
                area2 = area2 / area_tot

                vecs = tf[0 if (random() < area1) else 1]
            else:
                vecs = tf[0]

            u1 = random()
            u2 = random()
            u_tot = u1 + u2

            if u_tot > 1:
                u1 = 1.0 - u1
                u2 = 1.0 - u2

            side1 = vecs[1] - vecs[0]
            side2 = vecs[2] - vecs[0]

            p = vecs[0] + u1 * side1 + u2 * side2

            sampled_points[num_points * i + k] = p

    return sampled_points
