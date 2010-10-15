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

# <pep8 compliant>

import os
import time
import shutil

import bpy
import mathutils

def fixName(name):
    if name is None:
        return 'None'
    else:
        return name.replace(' ', '_')

def write_mtl(scene, filepath, copy_images, mtl_dict):

    world = scene.world
    worldAmb = world.ambient_color

    dest_dir = os.path.dirname(filepath)

    def copy_image(image):
        fn = bpy.path.abspath(image.filepath)
        fn = os.path.normpath(fn)
        fn_strip = os.path.basename(fn)

        if copy_images:
            rel = fn_strip
            fn_abs_dest = os.path.join(dest_dir, fn_strip)
            if not os.path.exists(fn_abs_dest):
                shutil.copy(fn, fn_abs_dest)
        elif bpy.path.is_subdir(fn, dest_dir):
            rel = os.path.relpath(fn, dest_dir)
        else:
            rel = fn

        return rel


    file = open(filepath, "w")
    # XXX
#   file.write('# Blender MTL File: %s\n' % Blender.Get('filepath').split('\\')[-1].split('/')[-1])
    file.write('# Material Count: %i\n' % len(mtl_dict))
    # Write material/image combinations we have used.
    for key, (mtl_mat_name, mat, img) in mtl_dict.items():

        # Get the Blender data for the material and the image.
        # Having an image named None will make a bug, dont do it :)

        file.write('newmtl %s\n' % mtl_mat_name) # Define a new material: matname_imgname

        if mat:
            file.write('Ns %.6f\n' % ((mat.specular_hardness-1) * 1.9607843137254901) ) # Hardness, convert blenders 1-511 to MTL's
            file.write('Ka %.6f %.6f %.6f\n' %  tuple([c*mat.ambient for c in worldAmb])  ) # Ambient, uses mirror colour,
            file.write('Kd %.6f %.6f %.6f\n' % tuple([c*mat.diffuse_intensity for c in mat.diffuse_color]) ) # Diffuse
            file.write('Ks %.6f %.6f %.6f\n' % tuple([c*mat.specular_intensity for c in mat.specular_color]) ) # Specular
            if hasattr(mat, "ior"):
                file.write('Ni %.6f\n' % mat.ior) # Refraction index
            else:
                file.write('Ni %.6f\n' % 1.0)
            file.write('d %.6f\n' % mat.alpha) # Alpha (obj uses 'd' for dissolve)

            # 0 to disable lighting, 1 for ambient & diffuse only (specular color set to black), 2 for full lighting.
            if mat.use_shadeless:
                file.write('illum 0\n') # ignore lighting
            elif mat.specular_intensity == 0:
                file.write('illum 1\n') # no specular.
            else:
                file.write('illum 2\n') # light normaly

        else:
            #write a dummy material here?
            file.write('Ns 0\n')
            file.write('Ka %.6f %.6f %.6f\n' %  tuple([c for c in worldAmb])  ) # Ambient, uses mirror colour,
            file.write('Kd 0.8 0.8 0.8\n')
            file.write('Ks 0.8 0.8 0.8\n')
            file.write('d 1\n') # No alpha
            file.write('illum 2\n') # light normaly

        # Write images!
        if img:  # We have an image on the face!
            # write relative image path
            rel = copy_image(img)
            file.write('map_Kd %s\n' % rel) # Diffuse mapping image
#           file.write('map_Kd %s\n' % img.filepath.split('\\')[-1].split('/')[-1]) # Diffuse mapping image

        elif mat: # No face image. if we havea material search for MTex image.
            for mtex in mat.texture_slots:
                if mtex and mtex.texture.type == 'IMAGE':
                    try:
                        filepath = copy_image(mtex.texture.image)
#                       filepath = mtex.texture.image.filepath.split('\\')[-1].split('/')[-1]
                        file.write('map_Kd %s\n' % repr(filepath)[1:-1]) # Diffuse mapping image
                        break
                    except:
                        # Texture has no image though its an image type, best ignore.
                        pass

        file.write('\n\n')

    file.close()

# XXX not used
def copy_file(source, dest):
    file = open(source, 'rb')
    data = file.read()
    file.close()

    file = open(dest, 'wb')
    file.write(data)
    file.close()


# XXX not used
def copy_images(dest_dir):
    if dest_dir[-1] != os.sep:
        dest_dir += os.sep
#   if dest_dir[-1] != sys.sep:
#       dest_dir += sys.sep

    # Get unique image names
    uniqueImages = {}
    for matname, mat, image in mtl_dict.values(): # Only use image name
        # Get Texface images
        if image:
            uniqueImages[image] = image # Should use sets here. wait until Python 2.4 is default.

        # Get MTex images
        if mat:
            for mtex in mat.texture_slots:
                if mtex and mtex.texture.type == 'IMAGE':
                    image_tex = mtex.texture.image
                    if image_tex:
                        try:
                            uniqueImages[image_tex] = image_tex
                        except:
                            pass

    # Now copy images
    copyCount = 0

#   for bImage in uniqueImages.values():
#       image_path = bpy.path.abspath(bImage.filepath)
#       if bpy.sys.exists(image_path):
#           # Make a name for the target path.
#           dest_image_path = dest_dir + image_path.split('\\')[-1].split('/')[-1]
#           if not bpy.utils.exists(dest_image_path): # Image isnt already there
#               print('\tCopying "%s" > "%s"' % (image_path, dest_image_path))
#               copy_file(image_path, dest_image_path)
#               copyCount+=1

#   paths= bpy.util.copy_images(uniqueImages.values(), dest_dir)

    print('\tCopied %d images' % copyCount)


def test_nurbs_compat(ob):
    if ob.type != 'CURVE':
        return False

    for nu in ob.data.splines:
        if nu.point_count_v == 1 and nu.type != 'BEZIER': # not a surface and not bezier
            return True

    return False


def write_nurb(file, ob, ob_mat):
    tot_verts = 0
    cu = ob.data

    # use negative indices
    for nu in cu.splines:
        if nu.type == 'POLY':
            DEG_ORDER_U = 1
        else:
            DEG_ORDER_U = nu.order_u - 1  # odd but tested to be correct

        if nu.type == 'BEZIER':
            print("\tWarning, bezier curve:", ob.name, "only poly and nurbs curves supported")
            continue

        if nu.point_count_v > 1:
            print("\tWarning, surface:", ob.name, "only poly and nurbs curves supported")
            continue

        if len(nu.points) <= DEG_ORDER_U:
            print("\tWarning, order_u is lower then vert count, skipping:", ob.name)
            continue

        pt_num = 0
        do_closed = nu.use_cyclic_u
        do_endpoints = (do_closed == 0) and nu.use_endpoint_u

        for pt in nu.points:
            pt = ob_mat * pt.co.copy().resize3D()
            file.write('v %.6f %.6f %.6f\n' % (pt[0], pt[1], pt[2]))
            pt_num += 1
        tot_verts += pt_num

        file.write('g %s\n' % (fixName(ob.name))) # fixName(ob.getData(1)) could use the data name too
        file.write('cstype bspline\n') # not ideal, hard coded
        file.write('deg %d\n' % DEG_ORDER_U) # not used for curves but most files have it still

        curve_ls = [-(i+1) for i in range(pt_num)]

        # 'curv' keyword
        if do_closed:
            if DEG_ORDER_U == 1:
                pt_num += 1
                curve_ls.append(-1)
            else:
                pt_num += DEG_ORDER_U
                curve_ls = curve_ls + curve_ls[0:DEG_ORDER_U]

        file.write('curv 0.0 1.0 %s\n' % (' '.join([str(i) for i in curve_ls]))) # Blender has no U and V values for the curve

        # 'parm' keyword
        tot_parm = (DEG_ORDER_U + 1) + pt_num
        tot_parm_div = float(tot_parm-1)
        parm_ls = [(i/tot_parm_div) for i in range(tot_parm)]

        if do_endpoints: # end points, force param
            for i in range(DEG_ORDER_U+1):
                parm_ls[i] = 0.0
                parm_ls[-(1+i)] = 1.0

        file.write('parm u %s\n' % ' '.join( [str(i) for i in parm_ls] ))

        file.write('end\n')

    return tot_verts

def write_file(filepath, objects, scene,
          EXPORT_TRI=False,
          EXPORT_EDGES=False,
          EXPORT_NORMALS=False,
          EXPORT_NORMALS_HQ=False,
          EXPORT_UV=True,
          EXPORT_MTL=True,
          EXPORT_COPY_IMAGES=False,
          EXPORT_APPLY_MODIFIERS=True,
          EXPORT_ROTX90=True,
          EXPORT_BLEN_OBS=True,
          EXPORT_GROUP_BY_OB=False,
          EXPORT_GROUP_BY_MAT=False,
          EXPORT_KEEP_VERT_ORDER=False,
          EXPORT_POLYGROUPS=False,
          EXPORT_CURVE_AS_NURBS=True):
    '''
    Basic write function. The context and options must be already set
    This can be accessed externaly
    eg.
    write( 'c:\\test\\foobar.obj', Blender.Object.GetSelected() ) # Using default options.
    '''

    # XXX
    import math

    def veckey3d(v):
        return round(v.x, 6), round(v.y, 6), round(v.z, 6)

    def veckey2d(v):
        return round(v[0], 6), round(v[1], 6)
        # return round(v.x, 6), round(v.y, 6)

    def findVertexGroupName(face, vWeightMap):
        """
        Searches the vertexDict to see what groups is assigned to a given face.
        We use a frequency system in order to sort out the name because a given vetex can
        belong to two or more groups at the same time. To find the right name for the face
        we list all the possible vertex group names with their frequency and then sort by
        frequency in descend order. The top element is the one shared by the highest number
        of vertices is the face's group
        """
        weightDict = {}
        for vert_index in face.vertices:
#       for vert in face:
            vWeights = vWeightMap[vert_index]
#           vWeights = vWeightMap[vert]
            for vGroupName, weight in vWeights:
                weightDict[vGroupName] = weightDict.get(vGroupName, 0) + weight

        if weightDict:
            alist = [(weight,vGroupName) for vGroupName, weight in weightDict.items()] # sort least to greatest amount of weight
            alist.sort()
            return(alist[-1][1]) # highest value last
        else:
            return '(null)'

    print('OBJ Export path: %r' % filepath)
    temp_mesh_name = '~tmp-mesh'

    time1 = time.clock()
#   time1 = sys.time()
#   scn = Scene.GetCurrent()

    file = open(filepath, "w")

    # Write Header
    file.write('# Blender v%s OBJ File: %r\n' % (bpy.app.version_string, os.path.basename(bpy.data.filepath)))
    file.write('# www.blender.org\n')

    # Tell the obj file what material file to use.
    if EXPORT_MTL:
        mtlfilepath = os.path.splitext(filepath)[0] + ".mtl"
        file.write('mtllib %s\n' % repr(os.path.basename(mtlfilepath))[1:-1]) # filepath can contain non utf8 chars, use repr

    if EXPORT_ROTX90:
        mat_xrot90= mathutils.Matrix.Rotation(-math.pi/2, 4, 'X')

    # Initialize totals, these are updated each object
    totverts = totuvco = totno = 1

    face_vert_index = 1

    globalNormals = {}

    # A Dict of Materials
    # (material.name, image.name):matname_imagename # matname_imagename has gaps removed.
    mtl_dict = {}

    # Get all meshes
    for ob_main in objects:

        # ignore dupli children
        if ob_main.parent and ob_main.parent.dupli_type != 'NONE':
            # XXX
            print(ob_main.name, 'is a dupli child - ignoring')
            continue

        obs = []
        if ob_main.dupli_type != 'NONE':
            # XXX
            print('creating dupli_list on', ob_main.name)
            ob_main.create_dupli_list(scene)

            obs = [(dob.object, dob.matrix) for dob in ob_main.dupli_list]

            # XXX debug print
            print(ob_main.name, 'has', len(obs), 'dupli children')
        else:
            obs = [(ob_main, ob_main.matrix_world)]

        for ob, ob_mat in obs:

            # Nurbs curve support
            if EXPORT_CURVE_AS_NURBS and test_nurbs_compat(ob):
                if EXPORT_ROTX90:
                   ob_mat = ob_mat * mat_xrot90
                totverts += write_nurb(file, ob, ob_mat)
                continue
            # END NURBS

            if ob.type != 'MESH':
                continue

            me = ob.create_mesh(scene, EXPORT_APPLY_MODIFIERS, 'PREVIEW')

            if EXPORT_ROTX90:
                me.transform(mat_xrot90 * ob_mat)
            else:
                me.transform(ob_mat)

#           # Will work for non meshes now! :)
#           me= BPyMesh.getMeshFromObject(ob, containerMesh, EXPORT_APPLY_MODIFIERS, EXPORT_POLYGROUPS, scn)
#           if not me:
#               continue

            if EXPORT_UV:
                faceuv = len(me.uv_textures) > 0
                if faceuv:
                    uv_layer = me.uv_textures.active.data[:]
            else:
                faceuv = False

            me_verts = me.vertices[:]

            # Make our own list so it can be sorted to reduce context switching
            face_index_pairs = [ (face, index) for index, face in enumerate(me.faces)]
            # faces = [ f for f in me.faces ]

            if EXPORT_EDGES:
                edges = me.edges
            else:
                edges = []

            if not (len(face_index_pairs)+len(edges)+len(me.vertices)): # Make sure there is somthing to write

                # clean up
                bpy.data.meshes.remove(me)

                continue # dont bother with this mesh.

            # XXX
            # High Quality Normals
            if EXPORT_NORMALS and face_index_pairs:
                me.calc_normals()
#               if EXPORT_NORMALS_HQ:
#                   BPyMesh.meshCalcNormals(me)
#               else:
#                   # transforming normals is incorrect
#                   # when the matrix is scaled,
#                   # better to recalculate them
#                   me.calcNormals()

            materials = me.materials

            materialNames = []
            materialItems = [m for m in materials]
            if materials:
                for mat in materials:
                    if mat:
                        materialNames.append(mat.name)
                    else:
                        materialNames.append(None)
                # Cant use LC because some materials are None.
                # materialNames = map(lambda mat: mat.name, materials) # Bug Blender, dosent account for null materials, still broken.

            # Possible there null materials, will mess up indicies
            # but at least it will export, wait until Blender gets fixed.
            materialNames.extend((16-len(materialNames)) * [None])
            materialItems.extend((16-len(materialItems)) * [None])

            # Sort by Material, then images
            # so we dont over context switch in the obj file.
            if EXPORT_KEEP_VERT_ORDER:
                pass
            elif faceuv:
                face_index_pairs.sort(key=lambda a: (a[0].material_index, hash(uv_layer[a[1]].image), a[0].use_smooth))
            elif len(materials) > 1:
                face_index_pairs.sort(key = lambda a: (a[0].material_index, a[0].use_smooth))
            else:
                # no materials
                face_index_pairs.sort(key = lambda a: a[0].use_smooth)
#           if EXPORT_KEEP_VERT_ORDER:
#               pass
#           elif faceuv:
#               try:    faces.sort(key = lambda a: (a.mat, a.image, a.use_smooth))
#               except: faces.sort(lambda a,b: cmp((a.mat, a.image, a.use_smooth), (b.mat, b.image, b.use_smooth)))
#           elif len(materials) > 1:
#               try:    faces.sort(key = lambda a: (a.mat, a.use_smooth))
#               except: faces.sort(lambda a,b: cmp((a.mat, a.use_smooth), (b.mat, b.use_smooth)))
#           else:
#               # no materials
#               try:    faces.sort(key = lambda a: a.use_smooth)
#               except: faces.sort(lambda a,b: cmp(a.use_smooth, b.use_smooth))

            # Set the default mat to no material and no image.
            contextMat = (0, 0) # Can never be this, so we will label a new material teh first chance we get.
            contextSmooth = None # Will either be true or false,  set bad to force initialization switch.

            if EXPORT_BLEN_OBS or EXPORT_GROUP_BY_OB:
                name1 = ob.name
                name2 = ob.data.name
                if name1 == name2:
                    obnamestring = fixName(name1)
                else:
                    obnamestring = '%s_%s' % (fixName(name1), fixName(name2))

                if EXPORT_BLEN_OBS:
                    file.write('o %s\n' % obnamestring) # Write Object name
                else: # if EXPORT_GROUP_BY_OB:
                    file.write('g %s\n' % obnamestring)


            # Vert
            for v in me_verts:
                file.write('v %.6f %.6f %.6f\n' % tuple(v.co))

            # UV
            if faceuv:
                uv_face_mapping = [[0,0,0,0] for i in range(len(face_index_pairs))] # a bit of a waste for tri's :/

                uv_dict = {} # could use a set() here
                uv_layer = me.uv_textures.active.data
                for f, f_index in face_index_pairs:
                    for uv_index, uv in enumerate(uv_layer[f_index].uv):
                        uvkey = veckey2d(uv)
                        try:
                            uv_face_mapping[f_index][uv_index] = uv_dict[uvkey]
                        except:
                            uv_face_mapping[f_index][uv_index] = uv_dict[uvkey] = len(uv_dict)
                            file.write('vt %.6f %.6f\n' % tuple(uv))

                uv_unique_count = len(uv_dict)
#               del uv, uvkey, uv_dict, f_index, uv_index
                # Only need uv_unique_count and uv_face_mapping

            # NORMAL, Smooth/Non smoothed.
            if EXPORT_NORMALS:
                for f, f_index in face_index_pairs:
                    if f.use_smooth:
                        for v_idx in f.vertices:
                            v = me_verts[v_idx]
                            noKey = veckey3d(v.normal)
                            if noKey not in globalNormals:
                                globalNormals[noKey] = totno
                                totno +=1
                                file.write('vn %.6f %.6f %.6f\n' % noKey)
                    else:
                        # Hard, 1 normal from the face.
                        noKey = veckey3d(f.normal)
                        if noKey not in globalNormals:
                            globalNormals[noKey] = totno
                            totno +=1
                            file.write('vn %.6f %.6f %.6f\n' % noKey)

            if not faceuv:
                f_image = None

            # XXX
            if EXPORT_POLYGROUPS:
                # Retrieve the list of vertex groups
                vertGroupNames = [g.name for g in ob.vertex_groups]

                currentVGroup = ''
                # Create a dictionary keyed by face id and listing, for each vertex, the vertex groups it belongs to
                vgroupsMap = [[] for _i in range(len(me_verts))]
                for v_idx, v in enumerate(me.vertices):
                    for g in v.groups:
                        vgroupsMap[v_idx].append((vertGroupNames[g.group], g.weight))

            for f, f_index in face_index_pairs:
                f_smooth= f.use_smooth
                f_mat = min(f.material_index, len(materialNames)-1)
#               f_mat = min(f.mat, len(materialNames)-1)
                if faceuv:

                    tface = uv_layer[f_index]

                    f_image = tface.image
                    f_uv = tface.uv
                    # f_uv= [tface.uv1, tface.uv2, tface.uv3]
                    # if len(f.vertices) == 4:
                    #   f_uv.append(tface.uv4)
#                   f_image = f.image
#                   f_uv= f.uv

                # MAKE KEY
                if faceuv and f_image: # Object is always true.
                    key = materialNames[f_mat],  f_image.name
                else:
                    key = materialNames[f_mat],  None # No image, use None instead.

                # Write the vertex group
                if EXPORT_POLYGROUPS:
                    if ob.vertex_groups:
                        # find what vertext group the face belongs to
                        theVGroup = findVertexGroupName(f,vgroupsMap)
                        if  theVGroup != currentVGroup:
                            currentVGroup = theVGroup
                            file.write('g %s\n' % theVGroup)

                # CHECK FOR CONTEXT SWITCH
                if key == contextMat:
                    pass # Context already switched, dont do anything
                else:
                    if key[0] is None and key[1] is None:
                        # Write a null material, since we know the context has changed.
                        if EXPORT_GROUP_BY_MAT:
                            # can be mat_image or (null)
                            file.write('g %s_%s\n' % (fixName(ob.name), fixName(ob.data.name)) ) # can be mat_image or (null)
                        file.write('usemtl (null)\n') # mat, image

                    else:
                        mat_data= mtl_dict.get(key)
                        if not mat_data:
                            # First add to global dict so we can export to mtl
                            # Then write mtl

                            # Make a new names from the mat and image name,
                            # converting any spaces to underscores with fixName.

                            # If none image dont bother adding it to the name
                            if key[1] is None:
                                mat_data = mtl_dict[key] = ('%s'%fixName(key[0])), materialItems[f_mat], f_image
                            else:
                                mat_data = mtl_dict[key] = ('%s_%s' % (fixName(key[0]), fixName(key[1]))), materialItems[f_mat], f_image

                        if EXPORT_GROUP_BY_MAT:
                            file.write('g %s_%s_%s\n' % (fixName(ob.name), fixName(ob.data.name), mat_data[0]) ) # can be mat_image or (null)

                        file.write('usemtl %s\n' % mat_data[0]) # can be mat_image or (null)

                contextMat = key
                if f_smooth != contextSmooth:
                    if f_smooth: # on now off
                        file.write('s 1\n')
                        contextSmooth = f_smooth
                    else: # was off now on
                        file.write('s off\n')
                        contextSmooth = f_smooth

                f_v_orig = [me_verts[v_idx] for v_idx in f.vertices]
                
                if not EXPORT_TRI or len(f_v_orig) == 3:
                    f_v_iter = (f_v_orig, )
                else:
                    f_v_iter = (f_v_orig[0], f_v_orig[1], f_v_orig[2]), (f_v_orig[0], f_v_orig[2], f_v_orig[3])

                # support for triangulation
                for f_v in f_v_iter:
                    file.write('f')

                    if faceuv:
                        if EXPORT_NORMALS:
                            if f_smooth: # Smoothed, use vertex normals
                                for vi, v in enumerate(f_v):
                                    file.write( ' %d/%d/%d' % \
                                                    (v.index + totverts,
                                                     totuvco + uv_face_mapping[f_index][vi],
                                                     globalNormals[ veckey3d(v.normal) ]) ) # vert, uv, normal

                            else: # No smoothing, face normals
                                no = globalNormals[ veckey3d(f.normal) ]
                                for vi, v in enumerate(f_v):
                                    file.write( ' %d/%d/%d' % \
                                                    (v.index + totverts,
                                                     totuvco + uv_face_mapping[f_index][vi],
                                                     no) ) # vert, uv, normal
                        else: # No Normals
                            for vi, v in enumerate(f_v):
                                file.write( ' %d/%d' % (\
                                  v.index + totverts,\
                                  totuvco + uv_face_mapping[f_index][vi])) # vert, uv

                        face_vert_index += len(f_v)

                    else: # No UV's
                        if EXPORT_NORMALS:
                            if f_smooth: # Smoothed, use vertex normals
                                for v in f_v:
                                    file.write( ' %d//%d' %
                                                (v.index + totverts, globalNormals[ veckey3d(v.normal) ]) )
                            else: # No smoothing, face normals
                                no = globalNormals[ veckey3d(f.normal) ]
                                for v in f_v:
                                    file.write( ' %d//%d' % (v.index + totverts, no) )
                        else: # No Normals
                            for v in f_v:
                                file.write( ' %d' % (v.index + totverts) )

                    file.write('\n')

            # Write edges.
            if EXPORT_EDGES:
                for ed in edges:
                    if ed.is_loose:
                        file.write('f %d %d\n' % (ed.vertices[0] + totverts, ed.vertices[1] + totverts))

            # Make the indicies global rather then per mesh
            totverts += len(me_verts)
            if faceuv:
                totuvco += uv_unique_count

            # clean up
            bpy.data.meshes.remove(me)

        if ob_main.dupli_type != 'NONE':
            ob_main.free_dupli_list()

    file.close()


    # Now we have all our materials, save them
    if EXPORT_MTL:
        write_mtl(scene, mtlfilepath, EXPORT_COPY_IMAGES, mtl_dict)
#   if EXPORT_COPY_IMAGES:
#       dest_dir = os.path.basename(filepath)
# #         dest_dir = filepath
# #         # Remove chars until we are just the path.
# #         while dest_dir and dest_dir[-1] not in '\\/':
# #             dest_dir = dest_dir[:-1]
#       if dest_dir:
#           copy_images(dest_dir, mtl_dict)
#       else:
#           print('\tError: "%s" could not be used as a base for an image path.' % filepath)

    print("OBJ Export time: %.2f" % (time.clock() - time1))

# 
def _write(context, filepath,
              EXPORT_TRI, # ok
              EXPORT_EDGES,
              EXPORT_NORMALS, # not yet
              EXPORT_NORMALS_HQ, # not yet
              EXPORT_UV, # ok
              EXPORT_MTL,
              EXPORT_COPY_IMAGES,
              EXPORT_APPLY_MODIFIERS, # ok
              EXPORT_ROTX90, # wrong
              EXPORT_BLEN_OBS,
              EXPORT_GROUP_BY_OB,
              EXPORT_GROUP_BY_MAT,
              EXPORT_KEEP_VERT_ORDER,
              EXPORT_POLYGROUPS,
              EXPORT_CURVE_AS_NURBS,
              EXPORT_SEL_ONLY, # ok
              EXPORT_ALL_SCENES, # XXX not working atm
              EXPORT_ANIMATION): # Not used
    
    base_name, ext = os.path.splitext(filepath)
    context_name = [base_name, '', '', ext] # Base name, scene name, frame number, extension

    orig_scene = context.scene

    # Exit edit mode before exporting, so current object states are exported properly.
    if bpy.ops.object.mode_set.poll():
        bpy.ops.object.mode_set(mode='OBJECT')

#   if EXPORT_ALL_SCENES:
#       export_scenes = bpy.data.scenes
#   else:
#       export_scenes = [orig_scene]

    # XXX only exporting one scene atm since changing
    # current scene is not possible.
    # Brecht says that ideally in 2.5 we won't need such a function,
    # allowing multiple scenes open at once.
    export_scenes = [orig_scene]

    # Export all scenes.
    for scene in export_scenes:
        #       scene.makeCurrent() # If already current, this is not slow.
        #       context = scene.getRenderingContext()
        orig_frame = scene.frame_current

        if EXPORT_ALL_SCENES: # Add scene name into the context_name
            context_name[1] = '_%s' % bpy.path.clean_name(scene.name) # WARNING, its possible that this could cause a collision. we could fix if were feeling parranoied.

        # Export an animation?
        if EXPORT_ANIMATION:
            scene_frames = range(scene.frame_start, scene.frame_end + 1) # Up to and including the end frame.
        else:
            scene_frames = [orig_frame] # Dont export an animation.

        # Loop through all frames in the scene and export.
        for frame in scene_frames:
            if EXPORT_ANIMATION: # Add frame to the filepath.
                context_name[2] = '_%.6d' % frame

            scene.frame_current = frame
            if EXPORT_SEL_ONLY:
                objects = context.selected_objects
            else:
                objects = scene.objects

            full_path= ''.join(context_name)

            # erm... bit of a problem here, this can overwrite files when exporting frames. not too bad.
            # EXPORT THE FILE.
            write_file(full_path, objects, scene,
                  EXPORT_TRI,
                  EXPORT_EDGES,
                  EXPORT_NORMALS,
                  EXPORT_NORMALS_HQ,
                  EXPORT_UV,
                  EXPORT_MTL,
                  EXPORT_COPY_IMAGES,
                  EXPORT_APPLY_MODIFIERS,
                  EXPORT_ROTX90,
                  EXPORT_BLEN_OBS,
                  EXPORT_GROUP_BY_OB,
                  EXPORT_GROUP_BY_MAT,
                  EXPORT_KEEP_VERT_ORDER,
                  EXPORT_POLYGROUPS,
                  EXPORT_CURVE_AS_NURBS)


        scene.frame_current = orig_frame

    # Restore old active scene.
#   orig_scene.makeCurrent()
#   Window.WaitCursor(0)


'''
Currently the exporter lacks these features:
* multiple scene export (only active scene is written)
* particles
'''


def save(operator, context, filepath="",
         use_triangles=False,
         use_edges=True,
         use_normals=False,
         use_hq_normals=False,
         use_uvs=True,
         use_materials=True,
         copy_images=False,
         use_modifiers=True,
         use_rotate_x90=True,
         use_blen_objects=True,
         group_by_object=False,
         group_by_material=False,
         keep_vertex_order=False,
         use_vertex_groups=False,
         use_nurbs=True,
         use_selection=True,
         use_all_scenes=False,
         use_animation=False,
         ):

    _write(context, filepath, 
           EXPORT_TRI=use_triangles,
           EXPORT_EDGES=use_edges,
           EXPORT_NORMALS=use_normals,
           EXPORT_NORMALS_HQ=use_hq_normals,
           EXPORT_UV=use_uvs,
           EXPORT_MTL=use_materials,
           EXPORT_COPY_IMAGES=copy_images,
           EXPORT_APPLY_MODIFIERS=use_modifiers,
           EXPORT_ROTX90=use_rotate_x90,
           EXPORT_BLEN_OBS=use_blen_objects,
           EXPORT_GROUP_BY_OB=group_by_object,
           EXPORT_GROUP_BY_MAT=group_by_material,
           EXPORT_KEEP_VERT_ORDER=keep_vertex_order,
           EXPORT_POLYGROUPS=use_vertex_groups,
           EXPORT_CURVE_AS_NURBS=use_nurbs,
           EXPORT_SEL_ONLY=use_selection,
           EXPORT_ALL_SCENES=use_all_scenes,
           EXPORT_ANIMATION=use_animation,
           )

    return {'FINISHED'}
