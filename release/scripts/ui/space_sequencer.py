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

# <pep8 compliant>
import bpy


def act_strip(context):
    try:
        return context.scene.sequence_editor.active_strip
    except AttributeError:
        return None


class SEQUENCER_HT_header(bpy.types.Header):
    bl_space_type = 'SEQUENCE_EDITOR'

    def draw(self, context):
        layout = self.layout

        st = context.space_data

        row = layout.row(align=True)
        row.template_header()

        if context.area.show_menus:
            sub = row.row(align=True)
            sub.menu("SEQUENCER_MT_view")

            row.separator()

            if (st.view_type == 'SEQUENCER') or (st.view_type == 'SEQUENCER_PREVIEW'):
                sub.menu("SEQUENCER_MT_select")
                sub.menu("SEQUENCER_MT_marker")
                sub.menu("SEQUENCER_MT_add")
                sub.menu("SEQUENCER_MT_strip")

        layout.prop(st, "view_type", text="")

        if (st.view_type == 'PREVIEW') or (st.view_type == 'SEQUENCER_PREVIEW'):
            layout.prop(st, "display_mode", text="")

        if (st.view_type == 'SEQUENCER'):
            row = layout.row(align=True)
            row.operator("sequencer.copy", text="", icon='COPYDOWN')
            row.operator("sequencer.paste", text="", icon='PASTEDOWN')

            layout.separator()
            layout.operator("sequencer.refresh_all")
        elif (st.view_type == 'SEQUENCER_PREVIEW'):
            layout.separator()
            layout.operator("sequencer.refresh_all")
            layout.prop(st, "display_channel", text="Channel")
        else:
            layout.prop(st, "display_channel", text="Channel")


class SEQUENCER_MT_view_toggle(bpy.types.Menu):
    bl_label = "View Type"

    def draw(self, context):
        layout = self.layout

        layout.operator("sequencer.view_toggle").type = 'SEQUENCER'
        layout.operator("sequencer.view_toggle").type = 'PREVIEW'
        layout.operator("sequencer.view_toggle").type = 'SEQUENCER_PREVIEW'


class SEQUENCER_MT_view(bpy.types.Menu):
    bl_label = "View"

    def draw(self, context):
        layout = self.layout

        st = context.space_data

        layout.column()

        """
    uiBlock *block= uiBeginBlock(C, ar, "seq_viewmenu", UI_EMBOSSP);
    short yco= 0, menuwidth=120;

    if (sseq->mainb == SEQ_DRAW_SEQUENCE) {
        uiDefIconTextBut(block, BUTM, 1, ICON_BLANK1,
                 "Play Back Animation "
                 "in all Sequence Areas|Alt A", 0, yco-=20,
                 menuwidth, 19, NULL, 0.0, 0.0, 1, 1, "");
    }
    else {
        uiDefIconTextBut(block, BUTM, 1, ICON_MENU_PANEL,
                 "Grease Pencil...", 0, yco-=20,
                 menuwidth, 19, NULL, 0.0, 0.0, 1, 7, "");
        uiDefMenuSep(block);

        uiDefIconTextBut(block, BUTM, 1, ICON_BLANK1,
                 "Play Back Animation "
                 "in this window|Alt A", 0, yco-=20,
                 menuwidth, 19, NULL, 0.0, 0.0, 1, 1, "");
    }
    uiDefIconTextBut(block, BUTM, 1, ICON_BLANK1,
             "Play Back Animation in all "
             "3D Views and Sequence Areas|Alt Shift A",
             0, yco-=20,
             menuwidth, 19, NULL, 0.0, 0.0, 1, 2, "");

        """
        layout.separator()
        if (st.view_type == 'SEQUENCER') or (st.view_type == 'SEQUENCER_PREVIEW'):
            layout.operator("sequencer.view_all", text='View all Sequences')
        if (st.view_type == 'PREVIEW') or (st.view_type == 'SEQUENCER_PREVIEW'):
            layout.operator("sequencer.view_all_preview", text='Fit preview in window')
        layout.operator("sequencer.view_selected")

        layout.prop(st, "draw_frames")
        layout.prop(st, "show_cframe_indicator")
        if st.display_mode == 'IMAGE':
            layout.prop(st, "draw_safe_margin")
        if st.display_mode == 'WAVEFORM':
            layout.prop(st, "separate_color_preview")

        layout.separator()

        layout.operator("screen.area_dupli")
        layout.operator("screen.screen_full_area")


class SEQUENCER_MT_select(bpy.types.Menu):
    bl_label = "Select"

    def draw(self, context):
        layout = self.layout

        layout.column()
        layout.operator("sequencer.select_active_side", text="Strips to the Left").side = 'LEFT'
        layout.operator("sequencer.select_active_side", text="Strips to the Right").side = 'RIGHT'
        layout.separator()
        layout.operator("sequencer.select_handles", text="Surrounding Handles").side = 'BOTH'
        layout.operator("sequencer.select_handles", text="Left Handle").side = 'LEFT'
        layout.operator("sequencer.select_handles", text="Right Handle").side = 'RIGHT'
        layout.separator()
        layout.operator("sequencer.select_linked")
        layout.operator("sequencer.select_all_toggle")
        layout.operator("sequencer.select_inverse")


class SEQUENCER_MT_marker(bpy.types.Menu):
    bl_label = "Marker"

    def draw(self, context):
        layout = self.layout

        layout.column()
        layout.operator("marker.add", text="Add Marker")
        layout.operator("marker.duplicate", text="Duplicate Marker")
        layout.operator("marker.move", text="Grab/Move Marker")
        layout.operator("marker.delete", text="Delete Marker")
        layout.separator()
        layout.label(text="ToDo: Name Marker")

        #layout.operator("sequencer.sound_strip_add", text="Transform Markers") # toggle, will be rna - (sseq->flag & SEQ_MARKER_TRANS)


class SEQUENCER_MT_add(bpy.types.Menu):
    bl_label = "Add"

    def draw(self, context):
        layout = self.layout
        layout.operator_context = 'INVOKE_REGION_WIN'

        layout.column()
        layout.operator("sequencer.scene_strip_add", text="Scene")
        layout.operator("sequencer.movie_strip_add", text="Movie")
        layout.operator("sequencer.image_strip_add", text="Image")
        layout.operator("sequencer.sound_strip_add", text="Sound")

        layout.menu("SEQUENCER_MT_add_effect")


class SEQUENCER_MT_add_effect(bpy.types.Menu):
    bl_label = "Effect Strip..."

    def draw(self, context):
        layout = self.layout
        layout.operator_context = 'INVOKE_REGION_WIN'

        layout.column()
        layout.operator("sequencer.effect_strip_add", text="Add").type = 'ADD'
        layout.operator("sequencer.effect_strip_add", text="Subtract").type = 'SUBTRACT'
        layout.operator("sequencer.effect_strip_add", text="Alpha Over").type = 'ALPHA_OVER'
        layout.operator("sequencer.effect_strip_add", text="Alpha Under").type = 'ALPHA_UNDER'
        layout.operator("sequencer.effect_strip_add", text="Cross").type = 'CROSS'
        layout.operator("sequencer.effect_strip_add", text="Gamma Cross").type = 'GAMMA_CROSS'
        layout.operator("sequencer.effect_strip_add", text="Multiply").type = 'MULTIPLY'
        layout.operator("sequencer.effect_strip_add", text="Over Drop").type = 'OVER_DROP'
        layout.operator("sequencer.effect_strip_add", text="Plugin").type = 'PLUGIN'
        layout.operator("sequencer.effect_strip_add", text="Wipe").type = 'WIPE'
        layout.operator("sequencer.effect_strip_add", text="Glow").type = 'GLOW'
        layout.operator("sequencer.effect_strip_add", text="Transform").type = 'TRANSFORM'
        layout.operator("sequencer.effect_strip_add", text="Color").type = 'COLOR'
        layout.operator("sequencer.effect_strip_add", text="Speed Control").type = 'SPEED'


class SEQUENCER_MT_strip(bpy.types.Menu):
    bl_label = "Strip"

    def draw(self, context):
        layout = self.layout

        layout.operator_context = 'INVOKE_REGION_WIN'

        layout.column()
        layout.operator("transform.transform", text="Grab/Move").mode = 'TRANSLATION'
        layout.operator("transform.transform", text="Grab/Extend from frame").mode = 'TIME_EXTEND'
        #  uiItemO(layout, NULL, 0, "sequencer.strip_snap"); // TODO - add this operator
        layout.separator()

        layout.operator("sequencer.cut", text="Cut (hard) at frame").type = 'HARD'
        layout.operator("sequencer.cut", text="Cut (soft) at frame").type = 'SOFT'
        layout.operator("sequencer.images_separate")
        layout.separator()

        layout.operator("sequencer.duplicate")
        layout.operator("sequencer.delete")

        strip = act_strip(context)

        if strip:
            stype = strip.type

            if	stype == 'EFFECT':
                layout.separator()
                layout.operator("sequencer.effect_change")
                layout.operator("sequencer.effect_reassign_inputs")
            elif stype == 'IMAGE':
                layout.separator()
                # layout.operator("sequencer.image_change")
                layout.operator("sequencer.rendersize")
            elif stype == 'SCENE':
                layout.separator()
                layout.operator("sequencer.scene_change", text="Change Scene")
            elif stype == 'MOVIE':
                layout.separator()
                layout.operator("sequencer.movie_change")
                layout.operator("sequencer.rendersize")

        layout.separator()

        layout.operator("sequencer.meta_make")
        layout.operator("sequencer.meta_separate")

        #if (ed && (ed->metastack.first || (ed->act_seq && ed->act_seq->type == SEQ_META))) {
        #	uiItemS(layout);
        #	uiItemO(layout, NULL, 0, "sequencer.meta_toggle");
        #}

        layout.separator()
        layout.operator("sequencer.reload")
        layout.separator()
        layout.operator("sequencer.lock")
        layout.operator("sequencer.unlock")
        layout.operator("sequencer.mute")
        layout.operator("sequencer.unmute")

        layout.operator("sequencer.mute", text="Mute Deselected Strips").unselected = True

        layout.operator("sequencer.snap")

        layout.operator_menu_enum("sequencer.swap", "side")


class SequencerButtonsPanel(bpy.types.Panel):
    bl_space_type = 'SEQUENCE_EDITOR'
    bl_region_type = 'UI'

    def has_sequencer(self, context):
        return (context.space_data.view_type == 'SEQUENCER') or (context.space_data.view_type == 'SEQUENCER_PREVIEW')

    def poll(self, context):
        return self.has_sequencer(context) and (act_strip(context) is not None)


class SequencerButtonsPanel_Output(bpy.types.Panel):
    bl_space_type = 'SEQUENCE_EDITOR'
    bl_region_type = 'UI'

    def has_preview(self, context):
        return (context.space_data.view_type == 'PREVIEW') or (context.space_data.view_type == 'SEQUENCER_PREVIEW')

    def poll(self, context):
        return self.has_preview(context)


class SEQUENCER_PT_edit(SequencerButtonsPanel):
    bl_label = "Edit Strip"

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        split = layout.split(percentage=0.3)
        split.label(text="Name:")
        split.prop(strip, "name", text="")

        split = layout.split(percentage=0.3)
        split.label(text="Type:")
        split.prop(strip, "type", text="")

        split = layout.split(percentage=0.3)
        split.label(text="Blend:")
        split.prop(strip, "blend_mode", text="")

        row = layout.row()
        if strip.mute == True:
            row.prop(strip, "mute", toggle=True, icon='RESTRICT_VIEW_ON', text="")
        elif strip.mute is False:
            row.prop(strip, "mute", toggle=True, icon='RESTRICT_VIEW_OFF', text="")

        sub = row.row()
        sub.active = (not strip.mute)

        sub.prop(strip, "blend_opacity", text="Opacity", slider=True)

        row = layout.row()
        row.prop(strip, "lock")
        row.prop(strip, "frame_locked", text="Frame Lock")

        col = layout.column()
        col.enabled = not strip.lock
        col.prop(strip, "channel")
        col.prop(strip, "start_frame")
        col.prop(strip, "length")

        col = layout.column(align=True)
        col.label(text="Offset:")
        col.prop(strip, "start_offset", text="Start")
        col.prop(strip, "end_offset", text="End")

        col = layout.column(align=True)
        col.label(text="Still:")
        col.prop(strip, "start_still", text="Start")
        col.prop(strip, "end_still", text="End")


class SEQUENCER_PT_effect(SequencerButtonsPanel):
    bl_label = "Effect Strip"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return strip.type in ('ADD', 'SUBTRACT', 'ALPHA_OVER', 'ALPHA_UNDER',
                              'CROSS', 'GAMMA_CROSS', 'MULTIPLY', 'OVER_DROP',
                              'PLUGIN',
                              'WIPE', 'GLOW', 'TRANSFORM', 'COLOR', 'SPEED')

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        if strip.type == 'COLOR':
            layout.prop(strip, "color")

        elif strip.type == 'WIPE':

            col = layout.column()
            col.prop(strip, "transition_type")
            col.label(text="Direction:")
            col.row().prop(strip, "direction", expand=True)

            col = layout.column()
            col.prop(strip, "blur_width", slider=True)
            if strip.transition_type in ('SINGLE', 'DOUBLE'):
                col.prop(strip, "angle")

        elif strip.type == 'GLOW':
            flow = layout.column_flow()
            flow.prop(strip, "threshold", slider=True)
            flow.prop(strip, "clamp", slider=True)
            flow.prop(strip, "boost_factor")
            flow.prop(strip, "blur_distance")

            row = layout.row()
            row.prop(strip, "quality", slider=True)
            row.prop(strip, "only_boost")

        elif strip.type == 'SPEED':
            layout.prop(strip, "global_speed")

            flow = layout.column_flow()
            flow.prop(strip, "curve_velocity")
            flow.prop(strip, "curve_compress_y")
            flow.prop(strip, "frame_blending")

        elif strip.type == 'TRANSFORM':
            self.draw_panel_transform(strip)


        col = layout.column(align=True)
        if strip.type == 'SPEED':
            col.prop(strip, "speed_fader", text="Speed fader")
        elif strip.type in ('CROSS', 'GAMMA_CROSS', 'PLUGIN', 'WIPE'):
                col.prop(strip, "use_effect_default_fade", "Default fade")
                if not strip.use_effect_default_fade:
                    col.prop(strip, "effect_fader", text="Effect fader")

    def draw_panel_transform(self, strip):
        layout = self.layout
        col = layout.column()

        col.prop(strip, "interpolation")
        col.prop(strip, "translation_unit")
        col = layout.column(align=True)
        col.label(text="Position:")
        col.prop(strip, "translate_start_x", text="X")
        col.prop(strip, "translate_start_y", text="Y")

        layout.separator()

        col = layout.column(align=True)
        col.prop(strip, "uniform_scale")
        if (strip.uniform_scale):
            col = layout.column(align=True)
            col.prop(strip, "scale_start_x", text="Scale")
        else:
            col = layout.column(align=True)
            col.label(text="Scale:")
            col.prop(strip, "scale_start_x", text="X")
            col.prop(strip, "scale_start_y", text="Y")

        layout.separator()

        col = layout.column(align=True)
        col.label(text="Rotation:")
        col.prop(strip, "rotation_start", text="Rotation")


class SEQUENCER_PT_input(SequencerButtonsPanel):
    bl_label = "Strip Input"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return strip.type in ('MOVIE', 'IMAGE')

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        split = layout.split(percentage=0.2)
        col = split.column()
        col.label(text="Path:")
        col = split.column()
        col.prop(strip, "directory", text="")

        # Current element for the filename

        elem = strip.getStripElem(context.scene.current_frame)
        if elem:
            split = layout.split(percentage=0.2)
            col = split.column()
            col.label(text="File:")
            col = split.column()
            col.prop(elem, "filename", text="") # strip.elements[0] could be a fallback

        layout.prop(strip, "use_translation", text="Image Offset:")
        if strip.transform:
            col = layout.column(align=True)
            col.active = strip.use_translation
            col.prop(strip.transform, "offset_x", text="X")
            col.prop(strip.transform, "offset_y", text="Y")

        layout.prop(strip, "use_crop", text="Image Crop:")
        if strip.crop:
            col = layout.column(align=True)
            col.active = strip.use_crop
            col.prop(strip.crop, "top")
            col.prop(strip.crop, "left")
            col.prop(strip.crop, "bottom")
            col.prop(strip.crop, "right")

        col = layout.column(align=True)
        col.label(text="Trim Duration:")
        col.prop(strip, "animation_start_offset", text="Start")
        col.prop(strip, "animation_end_offset", text="End")


class SEQUENCER_PT_sound(SequencerButtonsPanel):
    bl_label = "Sound"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return (strip.type == 'SOUND')

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        layout.template_ID(strip, "sound", open="sound.open")

        layout.separator()
        layout.prop(strip.sound, "filename", text="")

        row = layout.row()
        if strip.sound.packed_file:
            row.operator("sound.unpack", icon='PACKAGE', text="Unpack")
        else:
            row.operator("sound.pack", icon='UGLYPACKAGE', text="Pack")

        row.prop(strip.sound, "caching")

        layout.prop(strip, "volume")


class SEQUENCER_PT_scene(SequencerButtonsPanel):
    bl_label = "Scene"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return (strip.type == 'SCENE')

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        layout.template_ID(strip, "scene")


class SEQUENCER_PT_filter(SequencerButtonsPanel):
    bl_label = "Filter"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return strip.type in ('MOVIE', 'IMAGE', 'SCENE', 'META')

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        col = layout.column()
        col.label(text="Video:")
        col.prop(strip, "strobe")
        col.prop(strip, "de_interlace")

        col = layout.column()
        col.label(text="Colors:")
        col.prop(strip, "multiply_colors", text="Multiply")
        col.prop(strip, "premultiply")
        col.prop(strip, "convert_float")

        col = layout.column()
        col.label(text="Flip:")
        col.prop(strip, "flip_x", text="X")
        col.prop(strip, "flip_y", text="Y")
        col.prop(strip, "reverse_frames", text="Backwards")

        layout.prop(strip, "use_color_balance")
        if strip.color_balance: # TODO - need to add this somehow
            row = layout.row()
            row.active = strip.use_color_balance
            col = row.column()
            col.prop(strip.color_balance, "lift")
            col.prop(strip.color_balance, "inverse_lift", text="Inverse")
            col = row.column()
            col.prop(strip.color_balance, "gamma")
            col.prop(strip.color_balance, "inverse_gamma", text="Inverse")
            col = row.column()
            col.prop(strip.color_balance, "gain")
            col.prop(strip.color_balance, "inverse_gain", text="Inverse")


class SEQUENCER_PT_proxy(SequencerButtonsPanel):
    bl_label = "Proxy"

    def poll(self, context):
        if not self.has_sequencer(context):
            return False

        strip = act_strip(context)
        if not strip:
            return False

        return strip.type in ('MOVIE', 'IMAGE', 'SCENE', 'META')

    def draw_header(self, context):
        strip = act_strip(context)

        self.layout.prop(strip, "use_proxy", text="")

    def draw(self, context):
        layout = self.layout

        strip = act_strip(context)

        flow = layout.column_flow()
        flow.prop(strip, "proxy_custom_directory")
        if strip.proxy: # TODO - need to add this somehow
            flow.prop(strip.proxy, "directory")
            flow.prop(strip.proxy, "file")


class SEQUENCER_PT_view(SequencerButtonsPanel_Output):
    bl_label = "View Settings"

    def draw(self, context):
        layout = self.layout

        st = context.space_data

        col = layout.column()
        col.prop(st, "draw_overexposed") # text="Zebra"
        col.prop(st, "draw_safe_margin")

bpy.types.register(SEQUENCER_HT_header) # header/menu classes
bpy.types.register(SEQUENCER_MT_view)
bpy.types.register(SEQUENCER_MT_view_toggle)
bpy.types.register(SEQUENCER_MT_select)
bpy.types.register(SEQUENCER_MT_marker)
bpy.types.register(SEQUENCER_MT_add)
bpy.types.register(SEQUENCER_MT_add_effect)
bpy.types.register(SEQUENCER_MT_strip)

bpy.types.register(SEQUENCER_PT_edit) # sequencer panels
bpy.types.register(SEQUENCER_PT_effect)
bpy.types.register(SEQUENCER_PT_input)
bpy.types.register(SEQUENCER_PT_sound)
bpy.types.register(SEQUENCER_PT_scene)
bpy.types.register(SEQUENCER_PT_filter)
bpy.types.register(SEQUENCER_PT_proxy)

bpy.types.register(SEQUENCER_PT_view) # view panels
