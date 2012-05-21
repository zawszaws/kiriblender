/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2012 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation,
 *                 Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/mask/mask_ops.c
 *  \ingroup edmask
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_depsgraph.h"
#include "BKE_mask.h"
#include "BKE_tracking.h"

#include "DNA_mask_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"  /* SELECT */

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"
#include "ED_mask.h"
#include "ED_clip.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "mask_intern.h"  /* own include */

static int mask_parent_clear_exec(bContext *C, wmOperator *UNUSED(op))
{
	Mask *mask = CTX_data_edit_mask(C);
	MaskObject *maskobj;

	for (maskobj = mask->maskobjs.first; maskobj; maskobj = maskobj->next) {
		MaskSpline *spline;
		int i;

		for (spline = maskobj->splines.first; spline; spline = spline->next) {
			for (i = 0; i < spline->tot_point; i++) {
				MaskSplinePoint *point = &spline->points[i];

				if (MASKPOINT_ISSEL(point)) {
					point->parent.flag &= ~MASK_PARENT_ACTIVE;
				}
			}
		}
	}

	WM_event_add_notifier(C, NC_MASK | ND_DATA, mask);
	DAG_id_tag_update(&mask->id, 0);

	return OPERATOR_FINISHED;
}

void MASK_OT_parent_clear(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Clear Parent";
	ot->description = "Clear the masks parenting";
	ot->idname = "MASK_OT_parent_clear";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = mask_parent_clear_exec;

	ot->poll = ED_operator_object_active_editable;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int mask_parent_set_exec(bContext *C, wmOperator *UNUSED(op))
{
	Mask *mask = CTX_data_edit_mask(C);
	MaskObject *maskobj;

	/* parent info */
	SpaceClip *sc;
	MovieClip *clip;
	MovieTrackingTrack *track;
	MovieTrackingMarker *marker;
	MovieTrackingObject *tracking;
	/* done */

	float parmask_pos[2];

	if ((NULL == (sc = CTX_wm_space_clip(C))) ||
	    (NULL == (clip = sc->clip)) ||
	    (NULL == (track = clip->tracking.act_track)) ||
	    (NULL == (marker = BKE_tracking_get_marker(track, sc->user.framenr))) ||
	    (NULL == (tracking = BKE_tracking_active_object(&clip->tracking))))
	{
		return OPERATOR_CANCELLED;
	}

	BKE_mask_coord_from_movieclip(clip, &sc->user, parmask_pos, marker->pos);

	for (maskobj = mask->maskobjs.first; maskobj; maskobj = maskobj->next) {
		MaskSpline *spline;
		int i;

		for (spline = maskobj->splines.first; spline; spline = spline->next) {
			for (i = 0; i < spline->tot_point; i++) {
				MaskSplinePoint *point = &spline->points[i];

				if (MASKPOINT_ISSEL(point)) {
					BezTriple *bezt = &point->bezt;
					float tvec[2];

					point->parent.id_type = ID_MC;
					point->parent.id = &clip->id;
					strcpy(point->parent.parent, tracking->name);
					strcpy(point->parent.sub_parent, track->name);

					point->parent.flag |= MASK_PARENT_ACTIVE;

					sub_v2_v2v2(tvec, parmask_pos, bezt->vec[1]);

					add_v2_v2(bezt->vec[0], tvec);
					add_v2_v2(bezt->vec[1], tvec);
					add_v2_v2(bezt->vec[2], tvec);

					negate_v2_v2(point->parent.offset, tvec);
				}
			}
		}
	}

	WM_event_add_notifier(C, NC_MASK | ND_DATA, mask);
	DAG_id_tag_update(&mask->id, 0);

	return OPERATOR_FINISHED;
}

/** based on #OBJECT_OT_parent_set */
void MASK_OT_parent_set(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Make Parent";
	ot->description = "Set the masks parenting";
	ot->idname = "MASK_OT_parent_set";

	/* api callbacks */
	//ot->invoke = mask_parent_set_invoke;
	ot->exec = mask_parent_set_exec;

	ot->poll = ED_operator_object_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
