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
#  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
from functools import reduce

# TODO, have these in a more general module
from rna_prop_ui import rna_idprop_ui_get, rna_idprop_ui_prop_get

empty_layer = [False] * 16


def gen_none(obj, orig_bone_name):
    pass

def get_bone_data(obj, bone_name):
    arm = obj.data
    pbone = obj.pose.bones[bone_name]
    if obj.mode == 'EDIT':
        bone = arm.edit_bones[bone_name]
    else:
        bone = arm.bones[bone_name]
    
    return obj, arm, pbone, bone

def bone_basename(name):
    return name.split(".")[0]

def add_stretch_to(obj, from_name, to_name, name):
    '''
    Adds a bone that stretches from one to another
    '''
    
    is_editmode = (obj.mode == 'EDIT')
    if not is_editmode:
        bpy.ops.object.mode_set(mode='EDIT')

    arm = obj.data
    stretch_ebone = arm.edit_bones.new(name)
    stretch_name = stretch_ebone.name
    head = stretch_ebone.head = arm.edit_bones[from_name].head.copy()
    tail = stretch_ebone.tail = arm.edit_bones[to_name].head.copy()


    # Now for the constraint
    bpy.ops.object.mode_set(mode='OBJECT')
    from_pbone = obj.pose.bones[from_name]
    to_pbone = obj.pose.bones[to_name]
    stretch_pbone = obj.pose.bones[stretch_name]
    
    con = stretch_pbone.constraints.new('COPY_LOCATION')
    con.target = obj
    con.subtarget = from_name
    
    con = stretch_pbone.constraints.new('STRETCH_TO')
    con.target = obj
    con.subtarget = to_name   
    con.original_length = (head-tail).length
    con.keep_axis = 'PLANE_X'
    con.volume = 'NO_VOLUME'

    if is_editmode:
        bpy.ops.object.mode_set(mode='EDIT')
    #else:
    #    bpy.ops.object.mode_set(mode='OBJECT')


def gen_finger(obj, orig_bone_name):
    
    # *** EDITMODE
    
    # get assosiated data 
    obj, arm, orig_pbone, orig_ebone = get_bone_data(obj, orig_bone_name)
    
    obj.animation_data_create() # needed if its a new armature with no keys
    
    arm.layer[0] = arm.layer[8] = True
    
    children = orig_pbone.children_recursive
    tot_len = reduce(lambda f, pbone: f + pbone.bone.length, children, orig_pbone.bone.length)
    
    base_name = bone_basename(orig_pbone.name)
    
    # first make a new bone at the location of the finger
    control_ebone = arm.edit_bones.new(base_name)
    control_bone_name = control_ebone.name # we dont know if we get the name requested
    
    # Place the finger bone
    head = orig_ebone.head.copy()
    tail = orig_ebone.tail.copy()
    
    control_ebone.head = head
    control_ebone.tail = head + ((tail - head).normalize() * tot_len)
    control_ebone.roll = orig_ebone.roll
    
    # now add bones inbetween this and its children recursively
    
    # switching modes so store names only!
    children = [pbone.name for pbone in children]

    # set an alternate layer for driver bones
    other_layer = empty_layer[:]
    other_layer[8] = True
    

    driver_bone_pairs = []

    for child_bone_name in children:
        obj, arm, pbone_child, child_ebone = get_bone_data(obj, child_bone_name)
        
        # finger.02 --> finger_driver.02
        driver_bone_name = child_bone_name.split('.')
        driver_bone_name = driver_bone_name[0] + "_driver." + ".".join(driver_bone_name[1:])
        
        driver_ebone = arm.edit_bones.new(driver_bone_name)
        driver_bone_name = driver_ebone.name # cant be too sure!
        driver_ebone.layer = other_layer
        
        new_len = pbone_child.bone.length / 2.0

        head = child_ebone.head.copy()
        tail = child_ebone.tail.copy()
        
        driver_ebone.head = head
        driver_ebone.tail = head + ((tail - head).normalize() * new_len)
        driver_ebone.roll = child_ebone.roll
        
        # Insert driver_ebone in the chain without connected parents
        driver_ebone.connected = False
        driver_ebone.parent = child_ebone.parent
        
        child_ebone.connected = False
        child_ebone.parent = driver_ebone
        
        # Add the drivers to these when in posemode.
        driver_bone_pairs.append((child_bone_name, driver_bone_name))
    
    del control_ebone
    

    # *** POSEMODE
    bpy.ops.object.mode_set(mode='OBJECT')
    
    
    obj, arm, orig_pbone, orig_bone = get_bone_data(obj, orig_bone_name)
    obj, arm, control_pbone, control_bone= get_bone_data(obj, control_bone_name)
    
    
    # only allow Y scale
    control_pbone.lock_scale = (True, False, True)
    
    control_pbone["bend_ratio"]= 0.4
    prop = rna_idprop_ui_prop_get(control_pbone, "bend_ratio", create=True)
    prop["min"] = 0.0
    prop["max"] = 1.0
    
    con = orig_pbone.constraints.new('COPY_LOCATION')
    con.target = obj
    con.subtarget = control_bone_name
    
    con = orig_pbone.constraints.new('COPY_ROTATION')
    con.target = obj
    con.subtarget = control_bone_name
    
    
    
    # setup child drivers on each new smaller bone added. assume 2 for now.
    
    # drives the bones
    controller_path = control_pbone.path_to_id() # 'pose.bones["%s"]' % control_bone_name

    i = 0
    for child_bone_name, driver_bone_name in driver_bone_pairs:

        # XXX - todo, any number
        if i==2:
            break
        
        obj, arm, driver_pbone, driver_bone = get_bone_data(obj, driver_bone_name)
        
        driver_pbone.rotation_mode = 'YZX'
        fcurve_driver = driver_pbone.driver_add("rotation_euler", 0)
        
        #obj.driver_add('pose.bones["%s"].scale', 1)
        #obj.animation_data.drivers[-1] # XXX, WATCH THIS
        driver = fcurve_driver.driver
        
        # scale target
        tar = driver.targets.new()
        tar.name = "scale"
        tar.id_type = 'OBJECT'
        tar.id = obj
        tar.array_index = 1 # Y scale
        tar.rna_path = controller_path + '.scale'

        # bend target
        tar = driver.targets.new()
        tar.name = "br"
        tar.id_type = 'OBJECT'
        tar.id = obj
        tar.rna_path = controller_path + '["bend_ratio"]'

        # XXX - todo, any number
        if i==0:
            driver.expression = '(-scale+1.0)*pi*2.0*(1.0-br)'
        elif i==1:
            driver.expression = '(-scale+1.0)*pi*2.0*br'
        
        obj, arm, child_pbone, child_bone = get_bone_data(obj, child_bone_name)

        # only allow X rotation
        driver_pbone.lock_rotation = child_pbone.lock_rotation = (False, True, True)
        
        i += 1


def gen_delta(obj, delta_name):
    '''
    Use this bone to define a delta thats applied to its child in pose mode.
    '''
    
    mode_orig = obj.mode
    bpy.ops.object.mode_set(mode='OBJECT')
    
    delta_pbone = obj.pose.bones[delta_name]
    children = delta_pbone.children
    
    if len(children) != 1:
        print("only 1 child supported for delta")
    
    child_name = children[0].name
    
    delta_head = delta_pbone.head.copy()
    delta_tail = delta_pbone.tail.copy()
    delta_matrix = delta_pbone.matrix.copy()
    
    children = delta_pbone.children

    bpy.ops.object.mode_set(mode='EDIT')
    
    arm = obj.data
    
    # XXX -probably should allow via the UI
    for ebone in arm.edit_bones:
        ebone.selected = ebone.head_selected = ebone.tail_selected = False
    
    # Select for deleting
    delta_ebone = arm.edit_bones[delta_name]
    delta_ebone.selected = delta_ebone.head_selected = delta_ebone.tail_selected = True
    
    bpy.ops.armature.delete()
    
    bpy.ops.object.mode_set(mode='OBJECT')
    
    
    # Move the child bone to the deltas location
    obj.animation_data_create()
    child_pbone = obj.pose[child_name]
    
    # ------------------- drivers
    fcurve_driver = child_pbone.driver_add("rotation_euler", 0)
    #fcurve_driver = obj.animation_data.drivers[-1] # XXX, WATCH THIS
    driver = fcurve_driver.driver
    driver.type = 'AVERAGE'
    mod = driver.modifiers.new('GENERATOR')
    
    
    
    obj, arm, parent_pbone, parent_bone = get_bone_data(obj, delta_name)
    bpy.ops.object.mode_set(mode='EDIT')
    
    
    bpy.ops.object.mode_set(mode=mode_orig)


def gen_arm(obj, orig_bone_name):
    """
    the bone with the 'arm' property is the upper arm, this assumes a chain as follows.
    [shoulder, upper_arm, forearm, hand]
    ...where this bone is 'upper_arm'
    """
    
    def validate_chain():
        '''
        Sanity check and return the arm as a list of bone names.
        '''
        # do a sanity check
        obj, arm, orig_pbone, orig_ebone = get_bone_data(obj, orig_bone_name)
        shoulder_pbone = arm_pbone.parent
        
        if not shoulder_pbone:
            print("could not find 'arm' parent, skipping:", orig_bone_name)
            return

        # We could have some bones attached, find the bone that has this as its 2nd parent
        hands = []
        for pbone in obj.pose.bones:
            index = pbone.parent_index(orig_pbone)
            if index == 2:
                hands.append(pbone)

        if len(hands) > 1:
            print("more then 1 hand found on:", orig_bone_name)
            return

        # first add the 2 new bones
        hand_pbone = hands[0]
        forearm_pbone = hand_pbone.parent
        
        return shoulder_pbone.name, orig_pbone.name, forearm_pbone.name, hand_pbone.name
    
    shoulder_name, arm_name, forearm_name, hand_name = validate_chain()
    
    
    obj, arm, hand_pbone, hand_ebone = get_bone_data(obj, hand_name)
    
    # Add the edit bones
    hand_ik_ebone = arm.edit_bones.new(hand_name + "_ik")
    
    hand_ik_ebone.head = hand_ebone.head
    hand_ik_ebone.tail = hand_ebone.tail
    hand_ik_ebone.roll = hand_ebone.roll
    

gen_table = {
    "":gen_none, \
    "finger":gen_finger, \
    "delta":gen_delta, \
    "arm":gen_arm, \
}


def generate_rig(context, ob):
    
    # add_stretch_to(ob, "a", "b", "c")

    bpy.ops.object.mode_set(mode='OBJECT')
    
    
    # copy object and data
    ob.selected = False
    ob_new = ob.copy()
    ob_new.data = ob.data.copy()
    scene = context.scene
    scene.objects.link(ob_new)
    scene.objects.active = ob_new
    ob_new.selected = True
    
    # enter armature editmode
    
    
    for pbone_name in ob_new.pose.bones.keys():
        bone_type = ob_new.pose.bones[pbone_name].get("type", "")

        try:
            func = gen_table[bone_type]
        except KeyError:
            print("\tunknown type '%s', bone '%s'" % (bone_type, pbone_name))

        
        # Toggle editmode so the pose data is always up to date
        bpy.ops.object.mode_set(mode='EDIT')
        func(ob_new, pbone_name)
        bpy.ops.object.mode_set(mode='OBJECT')
    
    # needed to update driver deps
    # context.scene.update()

if __name__ == "__main__":
    generate_rig(bpy.context, bpy.context.object)
