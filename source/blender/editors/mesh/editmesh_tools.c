 /* $Id:
 *
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
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2004 by Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Johnny Matthews, Geoffrey Bantle.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/*

editmesh_tool.c: UI called tools for editmesh, geometry changes here, otherwise in mods.c

*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include "MEM_guardedalloc.h"
#include "PIL_time.h"

#include "BLO_sys_types.h" // for intptr_t support

#include "DNA_mesh_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_key_types.h"
#include "DNA_windowmanager_types.h"

#include "RNA_types.h"
#include "RNA_define.h"
#include "RNA_access.h"

#include "BLI_blenlib.h"
#include "BLI_arithb.h"
#include "BLI_editVert.h"
#include "BLI_rand.h"
#include "BLI_ghash.h"
#include "BLI_linklist.h"
#include "BLI_heap.h"

#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_depsgraph.h"
#include "BKE_global.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_utildefines.h"
#include "BKE_bmesh.h"
#include "BKE_report.h"
#include "BKE_tessmesh.h"


#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_mesh.h"
#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_util.h"
#include "ED_view3d.h"

#include "UI_interface.h"

#include "mesh_intern.h"
#include "bmesh.h"

/* XXX */
static void waitcursor(int val) {}
static int pupmenu() {return 0;}
#define add_numbut(a, b, c, d, e, f, g) {}

/* XXX */

/* local prototypes ---------------*/
static void free_tagged_edges_faces(EditMesh *em, EditEdge *eed, EditFace *efa);
int EdgeLoopDelete(EditMesh *em, wmOperator *op);

/********* qsort routines *********/


typedef struct xvertsort {
	float x;
	EditVert *v1;
} xvertsort;

static int vergxco(const void *v1, const void *v2)
{
	const xvertsort *x1=v1, *x2=v2;

	if( x1->x > x2->x ) return 1;
	else if( x1->x < x2->x) return -1;
	return 0;
}

struct facesort {
	uintptr_t x;
	struct EditFace *efa;
};


static int vergface(const void *v1, const void *v2)
{
	const struct facesort *x1=v1, *x2=v2;

	if( x1->x > x2->x ) return 1;
	else if( x1->x < x2->x) return -1;
	return 0;
}


/* *********************************** */

void convert_to_triface(EditMesh *em, int direction)
{
	EditFace *efa, *efan, *next;
	float fac;

	efa= em->faces.last;
	while(efa) {
		next= efa->prev;
		if(efa->v4) {
			if(efa->f & SELECT) {
				/* choose shortest diagonal for split */
				fac= VecLenf(efa->v1->co, efa->v3->co) - VecLenf(efa->v2->co, efa->v4->co);
				/* this makes sure exact squares get split different in both cases */
				if( (direction==0 && fac<FLT_EPSILON) || (direction && fac>0.0f) ) {
					efan= EM_face_from_faces(em, efa, NULL, 0, 1, 2, -1);
					if(efa->f & SELECT) EM_select_face(efan, 1);
					efan= EM_face_from_faces(em, efa, NULL, 0, 2, 3, -1);
					if(efa->f & SELECT) EM_select_face(efan, 1);
				}
				else {
					efan= EM_face_from_faces(em, efa, NULL, 0, 1, 3, -1);
					if(efa->f & SELECT) EM_select_face(efan, 1);
					efan= EM_face_from_faces(em, efa, NULL, 1, 2, 3, -1);
					if(efa->f & SELECT) EM_select_face(efan, 1);
				}

				BLI_remlink(&em->faces, efa);
				free_editface(em, efa);
			}
		}
		efa= next;
	}

	EM_fgon_flags(em);	// redo flags and indices for fgons


}

int removedoublesflag(EditMesh *em, short flag, short automerge, float limit)		/* return amount */
{
	/*
		flag -		Test with vert->flags
		automerge -	Alternative operation, merge unselected into selected.
					Used for "Auto Weld" mode. warning.
		limit -		Quick manhattan distance between verts.
	*/

	/* all verts with (flag & 'flag') are being evaluated */
	EditVert *eve, *v1, *nextve;
	EditEdge *eed, *e1, *nexted;
	EditFace *efa, *nextvl;
	xvertsort *sortblock, *sb, *sb1;
	struct facesort *vlsortblock, *vsb, *vsb1;
	int a, b, test, amount;


	/* flag 128 is cleared, count */

	/* Normal non weld operation */
	eve= em->verts.first;
	amount= 0;
	while(eve) {
		eve->f &= ~128;
		if(eve->h==0 && (automerge || (eve->f & flag))) amount++;
		eve= eve->next;
	}
	if(amount==0) return 0;

	/* allocate memory and qsort */
	sb= sortblock= MEM_mallocN(sizeof(xvertsort)*amount,"sortremovedoub");
	eve= em->verts.first;
	while(eve) {
		if(eve->h==0 && (automerge || (eve->f & flag))) {
			sb->x= eve->co[0]+eve->co[1]+eve->co[2];
			sb->v1= eve;
			sb++;
		}
		eve= eve->next;
	}
	qsort(sortblock, amount, sizeof(xvertsort), vergxco);


	/* test for doubles */
	sb= sortblock;
	if (automerge) {
		for(a=0; a<amount; a++, sb++) {
			eve= sb->v1;
			if( (eve->f & 128)==0 ) {
				sb1= sb+1;
				for(b=a+1; b<amount && (eve->f & 128)==0; b++, sb1++) {
					if(sb1->x - sb->x > limit) break;

					/* when automarge, only allow unselected->selected */
					v1= sb1->v1;
					if( (v1->f & 128)==0 ) {
						if ((eve->f & flag)==0 && (v1->f & flag)==1) {
							if(	(float)fabs(v1->co[0]-eve->co[0])<=limit &&
								(float)fabs(v1->co[1]-eve->co[1])<=limit &&
								(float)fabs(v1->co[2]-eve->co[2])<=limit)
							{	/* unique bit */
								eve->f|= 128;
								eve->tmp.v = v1;
							}
						} else if(	(eve->f & flag)==1 && (v1->f & flag)==0 ) {
							if(	(float)fabs(v1->co[0]-eve->co[0])<=limit &&
								(float)fabs(v1->co[1]-eve->co[1])<=limit &&
								(float)fabs(v1->co[2]-eve->co[2])<=limit)
							{	/* unique bit */
								v1->f|= 128;
								v1->tmp.v = eve;
							}
						}
					}
				}
			}
		}
	} else {
		for(a=0; a<amount; a++, sb++) {
			eve= sb->v1;
			if( (eve->f & 128)==0 ) {
				sb1= sb+1;
				for(b=a+1; b<amount; b++, sb1++) {
					/* first test: simpel dist */
					if(sb1->x - sb->x > limit) break;
					v1= sb1->v1;

					/* second test: is vertex allowed */
					if( (v1->f & 128)==0 ) {
						if(	(float)fabs(v1->co[0]-eve->co[0])<=limit &&
							(float)fabs(v1->co[1]-eve->co[1])<=limit &&
							(float)fabs(v1->co[2]-eve->co[2])<=limit)
						{
							v1->f|= 128;
							v1->tmp.v = eve;
						}
					}
				}
			}
		}
	}
	MEM_freeN(sortblock);

	if (!automerge)
		for(eve = em->verts.first; eve; eve=eve->next)
			if((eve->f & flag) && (eve->f & 128))
				EM_data_interp_from_verts(em, eve, eve->tmp.v, eve->tmp.v, 0.5f);

	/* test edges and insert again */
	eed= em->edges.first;
	while(eed) {
		eed->f2= 0;
		eed= eed->next;
	}
	eed= em->edges.last;
	while(eed) {
		nexted= eed->prev;

		if(eed->f2==0) {
			if( (eed->v1->f & 128) || (eed->v2->f & 128) ) {
				remedge(em, eed);

				if(eed->v1->f & 128) eed->v1 = eed->v1->tmp.v;
				if(eed->v2->f & 128) eed->v2 = eed->v2->tmp.v;
				e1= addedgelist(em, eed->v1, eed->v2, eed);

				if(e1) {
					e1->f2= 1;
					if(eed->f & SELECT)
						e1->f |= SELECT;
				}
				if(e1!=eed) free_editedge(em, eed);
			}
		}
		eed= nexted;
	}

	/* first count amount of test faces */
	efa= (struct EditFace *)em->faces.first;
	amount= 0;
	while(efa) {
		efa->f1= 0;
		if(efa->v1->f & 128) efa->f1= 1;
		else if(efa->v2->f & 128) efa->f1= 1;
		else if(efa->v3->f & 128) efa->f1= 1;
		else if(efa->v4 && (efa->v4->f & 128)) efa->f1= 1;

		if(efa->f1==1) amount++;
		efa= efa->next;
	}

	/* test faces for double vertices, and if needed remove them */
	efa= (struct EditFace *)em->faces.first;
	while(efa) {
		nextvl= efa->next;
		if(efa->f1==1) {

			if(efa->v1->f & 128) efa->v1= efa->v1->tmp.v;
			if(efa->v2->f & 128) efa->v2= efa->v2->tmp.v;
			if(efa->v3->f & 128) efa->v3= efa->v3->tmp.v;
			if(efa->v4 && (efa->v4->f & 128)) efa->v4= efa->v4->tmp.v;

			test= 0;
			if(efa->v1==efa->v2) test+=1;
			if(efa->v2==efa->v3) test+=2;
			if(efa->v3==efa->v1) test+=4;
			if(efa->v4==efa->v1) test+=8;
			if(efa->v3==efa->v4) test+=16;
			if(efa->v2==efa->v4) test+=32;

			if(test) {
				if(efa->v4) {
					if(test==1 || test==2) {
						efa->v2= efa->v3;
						efa->v3= efa->v4;
						efa->v4= 0;

						EM_data_interp_from_faces(em, efa, NULL, efa, 0, 2, 3, 3);

						test= 0;
					}
					else if(test==8 || test==16) {
						efa->v4= 0;
						test= 0;
					}
					else {
						BLI_remlink(&em->faces, efa);
						free_editface(em, efa);
						amount--;
					}
				}
				else {
					BLI_remlink(&em->faces, efa);
					free_editface(em, efa);
					amount--;
				}
			}

			if(test==0) {
				/* set edge pointers */
				efa->e1= findedgelist(em, efa->v1, efa->v2);
				efa->e2= findedgelist(em, efa->v2, efa->v3);
				if(efa->v4==0) {
					efa->e3= findedgelist(em, efa->v3, efa->v1);
					efa->e4= 0;
				}
				else {
					efa->e3= findedgelist(em, efa->v3, efa->v4);
					efa->e4= findedgelist(em, efa->v4, efa->v1);
				}
			}
		}
		efa= nextvl;
	}

	/* double faces: sort block */
	/* count again, now all selected faces */
	amount= 0;
	efa= em->faces.first;
	while(efa) {
		efa->f1= 0;
		if(faceselectedOR(efa, 1)) {
			efa->f1= 1;
			amount++;
		}
		efa= efa->next;
	}

	if(amount) {
		/* double faces: sort block */
		vsb= vlsortblock= MEM_mallocN(sizeof(struct facesort)*amount, "sortremovedoub");
		efa= em->faces.first;
		while(efa) {
			if(efa->f1 & 1) {
				if(efa->v4) vsb->x= (uintptr_t) MIN4( (uintptr_t)efa->v1, (uintptr_t)efa->v2, (uintptr_t)efa->v3, (uintptr_t)efa->v4);
				else vsb->x= (uintptr_t) MIN3( (uintptr_t)efa->v1, (uintptr_t)efa->v2, (uintptr_t)efa->v3);

				vsb->efa= efa;
				vsb++;
			}
			efa= efa->next;
		}

		qsort(vlsortblock, amount, sizeof(struct facesort), vergface);

		vsb= vlsortblock;
		for(a=0; a<amount; a++) {
			efa= vsb->efa;
			if( (efa->f1 & 128)==0 ) {
				vsb1= vsb+1;

				for(b=a+1; b<amount; b++) {

					/* first test: same pointer? */
					if(vsb->x != vsb1->x) break;

					/* second test: is test permitted? */
					efa= vsb1->efa;
					if( (efa->f1 & 128)==0 ) {
						if( compareface(efa, vsb->efa)) efa->f1 |= 128;

					}
					vsb1++;
				}
			}
			vsb++;
		}

		MEM_freeN(vlsortblock);

		/* remove double faces */
		efa= (struct EditFace *)em->faces.first;
		while(efa) {
			nextvl= efa->next;
			if(efa->f1 & 128) {
				BLI_remlink(&em->faces, efa);
				free_editface(em, efa);
			}
			efa= nextvl;
		}
	}

	/* remove double vertices */
	a= 0;
	eve= (struct EditVert *)em->verts.first;
	while(eve) {
		nextve= eve->next;
		if(automerge || eve->f & flag) {
			if(eve->f & 128) {
				a++;
				BLI_remlink(&em->verts, eve);
				free_editvert(em, eve);
			}
		}
		eve= nextve;
	}

	return a;	/* amount */
}

// XXX is this needed?
/* called from buttons */
static void xsortvert_flag__doSetX(void *userData, EditVert *eve, int x, int y, int index)
{
	xvertsort *sortblock = userData;

	sortblock[index].x = x;
}

/* all verts with (flag & 'flag') are sorted */
void xsortvert_flag(bContext *C, int flag)
{
#if 0 //BMESH_TODO
	ViewContext vc;
	EditVert *eve;
	xvertsort *sortblock;
	ListBase tbase;
	int i, amount;

	em_setup_viewcontext(C, &vc);

	amount = BLI_countlist(&vc.em->verts);
	sortblock = MEM_callocN(sizeof(xvertsort)*amount,"xsort");
	for (i=0,eve= vc.em->verts.first; eve; i++,eve=eve->next)
		if(eve->f & flag)
			sortblock[i].v1 = eve;

	mesh_foreachScreenVert(&vc, xsortvert_flag__doSetX, sortblock, 0);
	qsort(sortblock, amount, sizeof(xvertsort), vergxco);

		/* make temporal listbase */
	tbase.first= tbase.last= 0;
	for (i=0; i<amount; i++) {
		eve = sortblock[i].v1;

		if (eve) {
			BLI_remlink(&vc.em->verts, eve);
			BLI_addtail(&tbase, eve);
		}
	}

	addlisttolist(&vc.em->verts, &tbase);

	MEM_freeN(sortblock);
#endif
}

/* called from buttons */
void hashvert_flag(EditMesh *em, int flag)
{
	/* switch vertex order using hash table */
	EditVert *eve;
	struct xvertsort *sortblock, *sb, onth, *newsort;
	ListBase tbase;
	int amount, a, b;

	/* count */
	eve= em->verts.first;
	amount= 0;
	while(eve) {
		if(eve->f & flag) amount++;
		eve= eve->next;
	}
	if(amount==0) return;

	/* allocate memory */
	sb= sortblock= (struct xvertsort *)MEM_mallocN(sizeof(struct xvertsort)*amount,"sortremovedoub");
	eve= em->verts.first;
	while(eve) {
		if(eve->f & flag) {
			sb->v1= eve;
			sb++;
		}
		eve= eve->next;
	}

	BLI_srand(1);

	sb= sortblock;
	for(a=0; a<amount; a++, sb++) {
		b= (int)(amount*BLI_drand());
		if(b>=0 && b<amount) {
			newsort= sortblock+b;
			onth= *sb;
			*sb= *newsort;
			*newsort= onth;
		}
	}

	/* make temporal listbase */
	tbase.first= tbase.last= 0;
	sb= sortblock;
	while(amount--) {
		eve= sb->v1;
		BLI_remlink(&em->verts, eve);
		BLI_addtail(&tbase, eve);
		sb++;
	}

	addlisttolist(&em->verts, &tbase);

	MEM_freeN(sortblock);

}

/* generic extern called extruder */
void extrude_mesh(Scene *scene, Object *obedit, EditMesh *em, wmOperator *op)
{
	float nor[3]= {0.0, 0.0, 0.0};
	short nr, transmode= 0;

	/* extrude depends on totvertsel etc */
	EM_stats_update(em);
	
	if(em->selectmode & SCE_SELECT_VERTEX) {
		if(em->totvertsel==0) nr= 0;
		else if(em->totvertsel==1) nr= 4;
		else if(em->totedgesel==0) nr= 4;
		else if(em->totfacesel==0)
			nr= 3; // pupmenu("Extrude %t|Only Edges%x3|Only Vertices%x4");
		else if(em->totfacesel==1)
			nr= 1; // pupmenu("Extrude %t|Region %x1|Only Edges%x3|Only Vertices%x4");
		else
			nr= 1; // pupmenu("Extrude %t|Region %x1||Individual Faces %x2|Only Edges%x3|Only Vertices%x4");
	}
	else if(em->selectmode & SCE_SELECT_EDGE) {
		if (em->totedgesel==0) nr = 0;
		
		nr = 1;
		/*else if (em->totedgesel==1) nr = 3;
		else if(em->totfacesel==0) nr = 3;
		else if(em->totfacesel==1)
			nr= 1; // pupmenu("Extrude %t|Region %x1|Only Edges%x3");
		else
			nr= 1; // pupmenu("Extrude %t|Region %x1||Individual Faces %x2|Only Edges%x3");
		*/
	}
	else {
		if (em->totfacesel == 0) nr = 0;
		else if (em->totfacesel == 1) nr = 1;
		else
			nr= 1; // pupmenu("Extrude %t|Region %x1||Individual Faces %x2");
	}

	if(nr<1) return;

	if(nr==1)  transmode= extrudeflag(obedit, em, SELECT, nor);
	else if(nr==4) transmode= extrudeflag_verts_indiv(em, SELECT, nor);
	else if(nr==3) transmode= extrudeflag_edges_indiv(em, SELECT, nor);
	else transmode= extrudeflag_face_indiv(em, SELECT, nor);

	if(transmode==0) {
		BKE_report(op->reports, RPT_ERROR, "Not a valid selection for extrude");
	}
	else {
		EM_fgon_flags(em);

			/* We need to force immediate calculation here because
			* transform may use derived objects (which are now stale).
			*
			* This shouldn't be necessary, derived queries should be
			* automatically building this data if invalid. Or something.
			*/
//		DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
		object_handle_update(scene, obedit);

		/* individual faces? */
//		BIF_TransformSetUndo("Extrude");
		if(nr==2) {
//			initTransform(TFM_SHRINKFATTEN, CTX_NO_PET|CTX_NO_MIRROR);
//			Transform();
		}
		else {
//			initTransform(TFM_TRANSLATION, CTX_NO_PET|CTX_NO_MIRROR);
			if(transmode=='n') {
				Mat4MulVecfl(obedit->obmat, nor);
				VecSubf(nor, nor, obedit->obmat[3]);
//				BIF_setSingleAxisConstraint(nor, "along normal");
			}
//			Transform();
		}
	}

}

#if 0 
//need to see if this really had new stuff I should merge over
// XXX should be a menu item
static int mesh_extrude_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	Scene *scene= CTX_data_scene(C);
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);
//	int constraint_axis[3] = {0, 0, 1};

	extrude_mesh(scene, obedit, em, op);

	BKE_mesh_end_editmesh(obedit->data, em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	RNA_enum_set(op->ptr, "proportional", 0);
	RNA_boolean_set(op->ptr, "mirror", 0);
	
	/* the following two should only be set when extruding faces */
//	RNA_enum_set(op->ptr, "constraint_orientation", V3D_MANIP_NORMAL);
//	RNA_boolean_set_array(op->ptr, "constraint_axis", constraint_axis);
	
	
//	WM_operator_name_call(C, "TFM_OT_translate", WM_OP_INVOKE_REGION_WIN, op->ptr);

	return OPERATOR_FINISHED;
}

/* extrude without transform */
static int mesh_extrude_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh(obedit->data);

	extrude_mesh(scene, obedit, em, op);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	BKE_mesh_end_editmesh(obedit->data, em);
	return OPERATOR_FINISHED;
}


void MESH_OT_extrude(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Extrude";
	ot->idname= "MESH_OT_extrude";

	/* api callbacks */
	ot->invoke= mesh_extrude_invoke;
	ot->exec= mesh_extrude_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* to give to transform */
	Properties_Proportional(ot);
	Properties_Constraints(ot);
	RNA_def_boolean(ot->srna, "mirror", 0, "Mirror Editing", "");
}
#endif

static int split_mesh(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	WM_cursor_wait(1);

	/* make duplicate first */
	adduplicateflag(em, SELECT);
	/* old faces have flag 128 set, delete them */
	delfaceflag(em, 128);
	recalc_editnormals(em);

	WM_cursor_wait(0);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	BKE_mesh_end_editmesh(obedit->data, em);
	return OPERATOR_FINISHED;
}

void MESH_OT_split(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Split";
	ot->idname= "MESH_OT_split";

	/* api callbacks */
	ot->exec= split_mesh;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

#if 0
//this also showed up in a merge, need to check if it
//needs changes ported over to new extrude code too
static int extrude_repeat_mesh(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	RegionView3D *rv3d = ED_view3d_context_rv3d(C);

	int steps = RNA_int_get(op->ptr,"steps");

	float offs = RNA_float_get(op->ptr,"offset");

	float dvec[3], tmat[3][3], bmat[3][3], nor[3]= {0.0, 0.0, 0.0};
	short a;

	/* dvec */
	dvec[0]= rv3d->persinv[2][0];
	dvec[1]= rv3d->persinv[2][1];
	dvec[2]= rv3d->persinv[2][2];
	Normalize(dvec);
	dvec[0]*= offs;
	dvec[1]*= offs;
	dvec[2]*= offs;

	/* base correction */
	Mat3CpyMat4(bmat, obedit->obmat);
	Mat3Inv(tmat, bmat);
	Mat3MulVecfl(tmat, dvec);

	for(a=0; a<steps; a++) {
		extrudeflag(obedit, em, SELECT, nor);
		translateflag(em, SELECT, dvec);
	}

	recalc_editnormals(em);

	EM_fgon_flags(em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	BKE_mesh_end_editmesh(obedit->data, em);
	return OPERATOR_FINISHED;
}

void MESH_OT_extrude_repeat(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Extrude Repeat Mesh";
	ot->idname= "MESH_OT_extrude_repeat";

	/* api callbacks */
	ot->exec= extrude_repeat_mesh;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* props */
	RNA_def_float(ot->srna, "offset", 2.0f, 0.0f, 100.0f, "Offset", "", 0.0f, FLT_MAX);
	RNA_def_int(ot->srna, "steps", 10, 0, 180, "Steps", "", 0, INT_MAX);
}
#endif
/* ************************** spin operator ******************** */


static int spin_mesh(bContext *C, wmOperator *op, float *dvec, int steps, float degr, int dupli )
{
	Object *obedit= CTX_data_edit_object(C);
	ToolSettings *ts= CTX_data_tool_settings(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);
	EditVert *eve,*nextve;
	float nor[3]= {0.0f, 0.0f, 0.0f};
	float si, n[3], q[4], cmat[3][3], imat[3][3], tmat[3][3];
	float cent[3], bmat[3][3];
	float phi;
	short a, ok= 1;

	RNA_float_get_array(op->ptr, "center", cent);

	/* imat and center and size */
	Mat3CpyMat4(bmat, obedit->obmat);
	Mat3Inv(imat,bmat);

	cent[0]-= obedit->obmat[3][0];
	cent[1]-= obedit->obmat[3][1];
	cent[2]-= obedit->obmat[3][2];
	Mat3MulVecfl(imat, cent);

	phi= degr*M_PI/360.0;
	phi/= steps;
	if(ts->editbutflag & B_CLOCKWISE) phi= -phi;

	RNA_float_get_array(op->ptr, "axis", n);
	Normalize(n);

	q[0]= (float)cos(phi);
	si= (float)sin(phi);
	q[1]= n[0]*si;
	q[2]= n[1]*si;
	q[3]= n[2]*si;
	QuatToMat3(q, cmat);

	Mat3MulMat3(tmat,cmat,bmat);
	Mat3MulMat3(bmat,imat,tmat);

	if(dupli==0)
		if(ts->editbutflag & B_KEEPORIG)
			adduplicateflag(em, 1);

	for(a=0; a<steps; a++) {
		if(dupli==0) ok= extrudeflag(obedit, em, SELECT, nor);
		else adduplicateflag(em, SELECT);

		if(ok==0)
			break;

		rotateflag(em, SELECT, cent, bmat);
		if(dvec) {
			Mat3MulVecfl(bmat,dvec);
			translateflag(em, SELECT, dvec);
		}
	}

	if(ok==0) {
		/* no vertices or only loose ones selected, remove duplicates */
		eve= em->verts.first;
		while(eve) {
			nextve= eve->next;
			if(eve->f & SELECT) {
				BLI_remlink(&em->verts,eve);
				free_editvert(em, eve);
			}
			eve= nextve;
		}
	}
	else {
		recalc_editnormals(em);

		EM_fgon_flags(em);

		DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	}

	BKE_mesh_end_editmesh(obedit->data, em);
	return ok;
}

static int spin_mesh_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	int ok;

	ok= spin_mesh(C, op, NULL, RNA_int_get(op->ptr,"steps"), RNA_float_get(op->ptr,"degrees"), RNA_boolean_get(op->ptr,"dupli"));
	if(ok==0) {
		BKE_report(op->reports, RPT_ERROR, "No valid vertices are selected");
		return OPERATOR_CANCELLED;
	}

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	return OPERATOR_FINISHED;
}

/* get center and axis, in global coords */
static int spin_mesh_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	RegionView3D *rv3d= ED_view3d_context_rv3d(C);

	RNA_float_set_array(op->ptr, "center", give_cursor(scene, v3d));
	RNA_float_set_array(op->ptr, "axis", rv3d->viewinv[2]);

	return spin_mesh_exec(C, op);
}

void MESH_OT_spin(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Spin";
	ot->idname= "MESH_OT_spin";

	/* api callbacks */
	ot->invoke= spin_mesh_invoke;
	ot->exec= spin_mesh_exec;
	ot->poll= EM_view3d_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* props */
	RNA_def_int(ot->srna, "steps", 9, 0, INT_MAX, "Steps", "Steps", 0, INT_MAX);
	RNA_def_boolean(ot->srna, "dupli", 0, "Dupli", "Make Duplicates");
	RNA_def_float(ot->srna, "degrees", 90.0f, -FLT_MAX, FLT_MAX, "Degrees", "Degrees", -360.0f, 360.0f);

	RNA_def_float_vector(ot->srna, "center", 3, NULL, -FLT_MAX, FLT_MAX, "Center", "Center in global view space", -FLT_MAX, FLT_MAX);
	RNA_def_float_vector(ot->srna, "axis", 3, NULL, -1.0f, 1.0f, "Axis", "Axis in global view space", -FLT_MAX, FLT_MAX);

}

static int screw_mesh_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);
	EditVert *eve,*v1=0,*v2=0;
	EditEdge *eed;
	float dvec[3], nor[3];
	int steps, turns;

	turns= RNA_int_get(op->ptr, "turns");
	steps= RNA_int_get(op->ptr, "steps");

	/* clear flags */
	for(eve= em->verts.first; eve; eve= eve->next)
		eve->f1= 0;

	/* edges set flags in verts */
	for(eed= em->edges.first; eed; eed= eed->next) {
		if(eed->v1->f & SELECT) {
			if(eed->v2->f & SELECT) {
				/* watch: f1 is a byte */
				if(eed->v1->f1<2) eed->v1->f1++;
				if(eed->v2->f1<2) eed->v2->f1++;
			}
		}
	}
	/* find two vertices with eve->f1==1, more or less is wrong */
	for(eve= em->verts.first; eve; eve= eve->next) {
		if(eve->f1==1) {
			if(v1==NULL) v1= eve;
			else if(v2==NULL) v2= eve;
			else {
				v1= NULL;
				break;
			}
		}
	}
	if(v1==NULL || v2==NULL) {
		BKE_report(op->reports, RPT_ERROR, "You have to select a string of connected vertices too");
		BKE_mesh_end_editmesh(obedit->data, em);
		return OPERATOR_CANCELLED;
	}

	/* calculate dvec */
	dvec[0]= ( v1->co[0]- v2->co[0] )/steps;
	dvec[1]= ( v1->co[1]- v2->co[1] )/steps;
	dvec[2]= ( v1->co[2]- v2->co[2] )/steps;

	VECCOPY(nor, obedit->obmat[2]);

	if(nor[0]*dvec[0]+nor[1]*dvec[1]+nor[2]*dvec[2]>0.000) {
		dvec[0]= -dvec[0];
		dvec[1]= -dvec[1];
		dvec[2]= -dvec[2];
	}

	if(spin_mesh(C, op, dvec, turns*steps, 360.0f*turns, 0)) {
		DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
		WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

		BKE_mesh_end_editmesh(obedit->data, em);
		return OPERATOR_FINISHED;
	}
	else {
		BKE_report(op->reports, RPT_ERROR, "No valid vertices are selected");
		BKE_mesh_end_editmesh(obedit->data, em);
		return OPERATOR_CANCELLED;
	}
}

/* get center and axis, in global coords */
static int screw_mesh_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	RegionView3D *rv3d= ED_view3d_context_rv3d(C);

	RNA_float_set_array(op->ptr, "center", give_cursor(scene, v3d));
	RNA_float_set_array(op->ptr, "axis", rv3d->viewinv[1]);

	return screw_mesh_exec(C, op);
}

void MESH_OT_screw(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Screw";
	ot->idname= "MESH_OT_screw";

	/* api callbacks */
	ot->invoke= screw_mesh_invoke;
	ot->exec= screw_mesh_exec;
	ot->poll= EM_view3d_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/*props */
	RNA_def_int(ot->srna, "steps", 9, 0, INT_MAX, "Steps", "Steps", 0, 256);
	RNA_def_int(ot->srna, "turns", 1, 0, INT_MAX, "Turns", "Turns", 0, 256);

	RNA_def_float_vector(ot->srna, "center", 3, NULL, -FLT_MAX, FLT_MAX, "Center", "Center in global view space", -FLT_MAX, FLT_MAX);
	RNA_def_float_vector(ot->srna, "axis", 3, NULL, -1.0f, 1.0f, "Axis", "Axis in global view space", -FLT_MAX, FLT_MAX);
}

static void erase_edges(EditMesh *em, ListBase *l)
{
	EditEdge *ed, *nexted;

	ed = (EditEdge *) l->first;
	while(ed) {
		nexted= ed->next;
		if( (ed->v1->f & SELECT) || (ed->v2->f & SELECT) ) {
			remedge(em, ed);
			free_editedge(em, ed);
		}
		ed= nexted;
	}
}

static void erase_faces(EditMesh *em, ListBase *l)
{
	EditFace *f, *nextf;

	f = (EditFace *) l->first;

	while(f) {
		nextf= f->next;
		if( faceselectedOR(f, SELECT) ) {
			BLI_remlink(l, f);
			free_editface(em, f);
		}
		f = nextf;
	}
}

static void erase_vertices(EditMesh *em, ListBase *l)
{
	EditVert *v, *nextv;

	v = (EditVert *) l->first;
	while(v) {
		nextv= v->next;
		if(v->f & 1) {
			BLI_remlink(l, v);
			free_editvert(em, v);
		}
		v = nextv;
	}
}

/*GB*/
/*-------------------------------------------------------------------------------*/
/*--------------------------- Edge Based Subdivide ------------------------------*/

#define EDGENEW	2
#define FACENEW	2
#define EDGEINNER  4
#define EDGEOLD  8

/*used by faceloop cut to select only edges valid for edge slide*/
#define DOUBLEOPFILL 16

/* calculates offset for co, based on fractal, sphere or smooth settings  */
static void alter_co(float *co, EditEdge *edge, float smooth, float fractal, int beauty, float perc)
{
	float vec1[3], fac;

	if(beauty & B_SMOOTH) {
		/* we calculate an offset vector vec1[], to be added to *co */
		float len, fac, nor[3], nor1[3], nor2[3];

		VecSubf(nor, edge->v1->co, edge->v2->co);
		len= 0.5f*Normalize(nor);

		VECCOPY(nor1, edge->v1->no);
		VECCOPY(nor2, edge->v2->no);

		/* cosine angle */
		fac= nor[0]*nor1[0] + nor[1]*nor1[1] + nor[2]*nor1[2] ;

		vec1[0]= fac*nor1[0];
		vec1[1]= fac*nor1[1];
		vec1[2]= fac*nor1[2];

		/* cosine angle */
		fac= -nor[0]*nor2[0] - nor[1]*nor2[1] - nor[2]*nor2[2] ;

		vec1[0]+= fac*nor2[0];
		vec1[1]+= fac*nor2[1];
		vec1[2]+= fac*nor2[2];

		/* falloff for multi subdivide */
		smooth *= sqrt(fabs(1.0f - 2.0f*fabs(0.5f-perc)));

		vec1[0]*= smooth*len;
		vec1[1]*= smooth*len;
		vec1[2]*= smooth*len;

		co[0] += vec1[0];
		co[1] += vec1[1];
		co[2] += vec1[2];
	}
	/*else if(beauty & B_SPHERE) { // subdivide sphere
		Normalize(co);
		co[0]*= smooth;
		co[1]*= smooth;
		co[2]*= smooth;
	}

	if(beauty & B_FRACTAL) {
		fac= fractal*VecLenf(edge->v1->co, edge->v2->co);
		vec1[0]= fac*(float)(0.5-BLI_drand());
		vec1[1]= fac*(float)(0.5-BLI_drand());
		vec1[2]= fac*(float)(0.5-BLI_drand());
		VecAddf(co, co, vec1);
	}*/
}

/* assumes in the edge is the correct interpolated vertices already */
/* percent defines the interpolation, smooth, fractal and beauty are for special options */
/* results in new vertex with correct coordinate, vertex normal and weight group info */
static EditVert *subdivide_edge_addvert(EditMesh *em, EditEdge *edge, float smooth, float fractal, int beauty, float percent)
{
	EditVert *ev;
	float co[3];

	co[0] = (edge->v2->co[0]-edge->v1->co[0])*percent + edge->v1->co[0];
	co[1] = (edge->v2->co[1]-edge->v1->co[1])*percent + edge->v1->co[1];
	co[2] = (edge->v2->co[2]-edge->v1->co[2])*percent + edge->v1->co[2];

	/* offset for smooth or sphere or fractal */
	alter_co(co, edge, smooth, fractal, beauty, percent);

	/* clip if needed by mirror modifier */
	if (edge->v1->f2) {
		if ( edge->v1->f2 & edge->v2->f2 & 1) {
			co[0]= 0.0f;
		}
		if ( edge->v1->f2 & edge->v2->f2 & 2) {
			co[1]= 0.0f;
		}
		if ( edge->v1->f2 & edge->v2->f2 & 4) {
			co[2]= 0.0f;
		}
	}

	ev = addvertlist(em, co, NULL);

	/* vert data (vgroups, ..) */
	EM_data_interp_from_verts(em, edge->v1, edge->v2, ev, percent);

	/* normal */
	ev->no[0] = (edge->v2->no[0]-edge->v1->no[0])*percent + edge->v1->no[0];
	ev->no[1] = (edge->v2->no[1]-edge->v1->no[1])*percent + edge->v1->no[1];
	ev->no[2] = (edge->v2->no[2]-edge->v1->no[2])*percent + edge->v1->no[2];
	Normalize(ev->no);

	return ev;
}

static void flipvertarray(EditVert** arr, short size)
{
	EditVert *hold;
	int i;

	for(i=0; i<size/2; i++) {
		hold = arr[i];
		arr[i] = arr[size-i-1];
		arr[size-i-1] = hold;
	}
}

static void facecopy(EditMesh *em, EditFace *source, EditFace *target)
{
	float *v1 = source->v1->co, *v2 = source->v2->co, *v3 = source->v3->co;
	float *v4 = source->v4? source->v4->co: NULL;
	float w[4][4];

	CustomData_em_copy_data(&em->fdata, &em->fdata, source->data, &target->data);

	target->mat_nr = source->mat_nr;
	target->flag   = source->flag;
	target->h	   = source->h;

	InterpWeightsQ3Dfl(v1, v2, v3, v4, target->v1->co, w[0]);
	InterpWeightsQ3Dfl(v1, v2, v3, v4, target->v2->co, w[1]);
	InterpWeightsQ3Dfl(v1, v2, v3, v4, target->v3->co, w[2]);
	if (target->v4)
		InterpWeightsQ3Dfl(v1, v2, v3, v4, target->v4->co, w[3]);

	CustomData_em_interp(&em->fdata, &source->data, NULL, (float*)w, 1, target->data);
}

static void fill_quad_single(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts, int seltype)
{
	EditEdge *cedge=NULL;
	EditVert *v[4], **verts;
	EditFace *hold;
	short start=0, end, left, right, vertsize,i;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT)	  { cedge = efa->e1; start = 0;}
	else if(efa->e2->f & SELECT) { cedge = efa->e2; start = 1;}
	else if(efa->e3->f & SELECT) { cedge = efa->e3; start = 2;}
	else if(efa->e4->f & SELECT) { cedge = efa->e4; start = 3;}

	// Point verts to the array of new verts for cedge
	verts = BLI_ghash_lookup(gh, cedge);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0] != v[start]) {flipvertarray(verts,numcuts+2);}
	end	= (start+1)%4;
	left   = (start+2)%4;
	right  = (start+3)%4;

	/*
	We should have something like this now

			  end		 start
			   3   2   1   0
			   |---*---*---|
			   |		   |
			   |		   |
			   |		   |
			   -------------
			  left	   right

	where start,end,left, right are indexes of EditFace->v1, etc (stored in v)
	and 0,1,2... are the indexes of the new verts stored in verts

	We will fill this case like this or this depending on even or odd cuts

			   |---*---*---|		  |---*---|
			   |  /	 \  |		  |  / \  |
			   | /	   \ |		  | /   \ |
			   |/		 \|		  |/	 \|
			   -------------		  ---------
	*/

	// Make center face
	if(vertsize % 2 == 0) {
		hold = addfacelist(em, verts[(vertsize-1)/2],verts[((vertsize-1)/2)+1],v[left],v[right], NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		hold->e4->f2 |= EDGEINNER;
	}else{
		hold = addfacelist(em, verts[(vertsize-1)/2],v[left],v[right],NULL, NULL,NULL);
		hold->e1->f2 |= EDGEINNER;
		hold->e3->f2 |= EDGEINNER;
	}
	facecopy(em, efa,hold);

	// Make side faces
	for(i=0;i<(vertsize-1)/2;i++) {
		hold = addfacelist(em, verts[i],verts[i+1],v[right],NULL,NULL,NULL);
		facecopy(em, efa,hold);
		if(i+1 != (vertsize-1)/2) {
            if(seltype == SUBDIV_SELECT_INNER) {
	 		   hold->e2->f2 |= EDGEINNER;
            }
		}
		hold = addfacelist(em, verts[vertsize-2-i],verts[vertsize-1-i],v[left],NULL,NULL,NULL);
		facecopy(em, efa,hold);
		if(i+1 != (vertsize-1)/2) {
            if(seltype == SUBDIV_SELECT_INNER) {
		 		hold->e3->f2 |= EDGEINNER;
            }
		}
	}
}

static void fill_tri_single(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts, int seltype)
{
	EditEdge *cedge=NULL;
	EditVert *v[3], **verts;
	EditFace *hold;
	short start=0, end, op, vertsize,i;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;

	if(efa->e1->f & SELECT)	  { cedge = efa->e1; start = 0;}
	else if(efa->e2->f & SELECT) { cedge = efa->e2; start = 1;}
	else if(efa->e3->f & SELECT) { cedge = efa->e3; start = 2;}

	// Point verts to the array of new verts for cedge
	verts = BLI_ghash_lookup(gh, cedge);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0] != v[start]) {flipvertarray(verts,numcuts+2);}
	   end	= (start+1)%3;
	   op	 = (start+2)%3;

	/*
	We should have something like this now

			  end		 start
			   3   2   1   0
			   |---*---*---|
			   \		   |
				 \		 |
				   \	   |
					 \	 |
					   \   |
						 \ |
						   |op

	where start,end,op are indexes of EditFace->v1, etc (stored in v)
	and 0,1,2... are the indexes of the new verts stored in verts

	We will fill this case like this or this depending on even or odd cuts

			   3   2   1   0
			   |---*---*---|
			   \	\  \   |
				 \	\ \  |
				   \   \ \ |
					 \  \ \|
					   \ \\|
						 \ |
						   |op
	*/

	// Make side faces
	for(i=0;i<(vertsize-1);i++) {
		hold = addfacelist(em, verts[i],verts[i+1],v[op],NULL,NULL,NULL);
		if(i+1 != vertsize-1) {
            if(seltype == SUBDIV_SELECT_INNER) {
		 		hold->e2->f2 |= EDGEINNER;
            }
		}
		facecopy(em, efa,hold);
	}
}

static void fill_quad_double_op(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[2]={NULL, NULL};
	EditVert *v[4], **verts[2];
	EditFace *hold;
	short start=0, end, left, right, vertsize,i;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT)	  { cedge[0] = efa->e1;  cedge[1] = efa->e3; start = 0;}
	else if(efa->e2->f & SELECT)	  { cedge[0] = efa->e2;  cedge[1] = efa->e4; start = 1;}

	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	end	= (start+1)%4;
	left   = (start+2)%4;
	right  = (start+3)%4;
	if(verts[1][0] != v[left]) {flipvertarray(verts[1],numcuts+2);}
	/*
	We should have something like this now

			  end		 start
			   3   2   1   0
			   |---*---*---|
			   |		   |
			   |		   |
			   |		   |
			   |---*---*---|
			   0   1   2   3
			  left	   right

	We will fill this case like this or this depending on even or odd cuts

			   |---*---*---|
			   |   |   |   |
			   |   |   |   |
			   |   |   |   |
			   |---*---*---|
	*/

	// Make side faces
	for(i=0;i<vertsize-1;i++) {
		hold = addfacelist(em, verts[0][i],verts[0][i+1],verts[1][vertsize-2-i],verts[1][vertsize-1-i],NULL,NULL);
		if(i < vertsize-2) {
			hold->e2->f2 |= EDGEINNER;
			hold->e2->f2 |= DOUBLEOPFILL;
		}
		facecopy(em, efa,hold);
	}
}

static void fill_quad_double_adj_path(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[2]={NULL, NULL};
	EditVert *v[4], **verts[2];
	EditFace *hold;
	short start=0, start2=0, vertsize,i;
	int ctrl= 0; // XXX

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT && efa->e2->f & SELECT) {cedge[0] = efa->e1;  cedge[1] = efa->e2; start = 0; start2 = 1;}
	if(efa->e2->f & SELECT && efa->e3->f & SELECT) {cedge[0] = efa->e2;  cedge[1] = efa->e3; start = 1; start2 = 2;}
	if(efa->e3->f & SELECT && efa->e4->f & SELECT) {cedge[0] = efa->e3;  cedge[1] = efa->e4; start = 2; start2 = 3;}
	if(efa->e4->f & SELECT && efa->e1->f & SELECT) {cedge[0] = efa->e4;  cedge[1] = efa->e1; start = 3; start2 = 0;}

	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != v[start2]) {flipvertarray(verts[1],numcuts+2);}
	/*
	We should have something like this now

			   end		 start
				3   2   1   0
		start2 0|---*---*---|
				|		   |
			   1*		   |
				|		   |
			   2*		   |
				|		   |
		 end2  3|-----------|

	We will fill this case like this or this depending on even or odd cuts
			   |---*---*---|
			   | /   /   / |
			   *   /   /   |
			   | /   /	 |
			   *   /	   |
			   | /		 |
			   |-----------|
	*/

	// Make outside tris
	hold = addfacelist(em, verts[0][vertsize-2],verts[0][vertsize-1],verts[1][1],NULL,NULL,NULL);
	/* when ctrl is depressed, only want verts on the cutline selected */
	if (ctrl)
		hold->e3->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	hold = addfacelist(em, verts[0][0],verts[1][vertsize-1],v[(start2+2)%4],NULL,NULL,NULL);
	/* when ctrl is depressed, only want verts on the cutline selected */
	if (ctrl)
		hold->e1->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	//if(scene->toolsettings->editbutflag & B_AUTOFGON) {
	//	hold->e1->h |= EM_FGON;
	//}
	// Make side faces

	for(i=0;i<numcuts;i++) {
		hold = addfacelist(em, verts[0][i],verts[0][i+1],verts[1][vertsize-1-(i+1)],verts[1][vertsize-1-i],NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
	}
	//EM_fgon_flags(em);

}

static void fill_quad_double_adj_fan(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[2]={NULL, NULL};
	EditVert *v[4], *op=NULL, **verts[2];
	EditFace *hold;
	short start=0, start2=0, vertsize,i;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT && efa->e2->f & SELECT) {cedge[0] = efa->e1;  cedge[1] = efa->e2; start = 0; start2 = 1; op = efa->v4;}
	if(efa->e2->f & SELECT && efa->e3->f & SELECT) {cedge[0] = efa->e2;  cedge[1] = efa->e3; start = 1; start2 = 2; op = efa->v1;}
	if(efa->e3->f & SELECT && efa->e4->f & SELECT) {cedge[0] = efa->e3;  cedge[1] = efa->e4; start = 2; start2 = 3; op = efa->v2;}
	if(efa->e4->f & SELECT && efa->e1->f & SELECT) {cedge[0] = efa->e4;  cedge[1] = efa->e1; start = 3; start2 = 0; op = efa->v3;}


	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != v[start2]) {flipvertarray(verts[1],numcuts+2);}
	/*
	We should have something like this now

			   end		 start
				3   2   1   0
		start2 0|---*---*---|
				|		   |
			   1*		   |
				|		   |
			   2*		   |
				|		   |
		 end2  3|-----------|op

	We will fill this case like this or this (warning horrible ascii art follows)
			   |---*---*---|
			   | \  \   \  |
			   *---\  \  \ |
			   |   \ \ \  \|
			   *---- \ \  \ |
			   |    ---  \\\|
			   |-----------|
	*/

	for(i=0;i<=numcuts;i++) {
		hold = addfacelist(em, op,verts[1][numcuts-i],verts[1][numcuts-i+1],NULL,NULL,NULL);
		hold->e1->f2 |= EDGEINNER;
		facecopy(em, efa,hold);

		hold = addfacelist(em, op,verts[0][i],verts[0][i+1],NULL,NULL,NULL);
		hold->e3->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
	}
}

static void fill_quad_double_adj_inner(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[2]={NULL, NULL};
	EditVert *v[4], *op=NULL, **verts[2],**inner;
	EditFace *hold;
	short start=0, start2=0, vertsize,i;
	float co[3];

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT && efa->e2->f & SELECT) {cedge[0] = efa->e1;  cedge[1] = efa->e2; start = 0; start2 = 1; op = efa->v4;}
	if(efa->e2->f & SELECT && efa->e3->f & SELECT) {cedge[0] = efa->e2;  cedge[1] = efa->e3; start = 1; start2 = 2; op = efa->v1;}
	if(efa->e3->f & SELECT && efa->e4->f & SELECT) {cedge[0] = efa->e3;  cedge[1] = efa->e4; start = 2; start2 = 3; op = efa->v2;}
	if(efa->e4->f & SELECT && efa->e1->f & SELECT) {cedge[0] = efa->e4;  cedge[1] = efa->e1; start = 3; start2 = 0; op = efa->v3;}


	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != v[start2]) {flipvertarray(verts[1],numcuts+2);}
	/*
	We should have something like this now

			   end		 start
				3   2   1   0
		start2 0|---*---*---|
				|		   |
			   1*		   |
				|		   |
			   2*		   |
				|		   |
		 end2  3|-----------|op

	We will fill this case like this or this (warning horrible ascii art follows)
			   |---*-----*---|
			   | *     /     |
			   *   \ /       |
			   |    *        |
			   | /	  \	     |
			   *        \    |
			   |           \ |
			   |-------------|
	*/

	// Add Inner Vert(s)
	inner = MEM_mallocN(sizeof(EditVert*)*numcuts,"New inner verts");

	for(i=0;i<numcuts;i++) {
		co[0] = (verts[0][numcuts-i]->co[0] + verts[1][i+1]->co[0] ) / 2 ;
		co[1] = (verts[0][numcuts-i]->co[1] + verts[1][i+1]->co[1] ) / 2 ;
		co[2] = (verts[0][numcuts-i]->co[2] + verts[1][i+1]->co[2] ) / 2 ;
		inner[i] = addvertlist(em, co, NULL);
		inner[i]->f2 |= EDGEINNER;

		EM_data_interp_from_verts(em, verts[0][numcuts-i], verts[1][i+1], inner[i], 0.5f);
	}

	// Add Corner Quad
	hold = addfacelist(em, verts[0][numcuts+1],verts[1][1],inner[0],verts[0][numcuts],NULL,NULL);
	hold->e2->f2 |= EDGEINNER;
	hold->e3->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	// Add Bottom Quads
	hold = addfacelist(em, verts[0][0],verts[0][1],inner[numcuts-1],op,NULL,NULL);
	hold->e2->f2 |= EDGEINNER;
	facecopy(em, efa,hold);

	hold = addfacelist(em, op,inner[numcuts-1],verts[1][numcuts],verts[1][numcuts+1],NULL,NULL);
	hold->e2->f2 |= EDGEINNER;
	facecopy(em, efa,hold);

	//if(scene->toolsettings->editbutflag & B_AUTOFGON) {
	//	hold->e1->h |= EM_FGON;
	//}
	// Add Fill Quads (if # cuts > 1)

	for(i=0;i<numcuts-1;i++) {
		hold = addfacelist(em, inner[i],verts[1][i+1],verts[1][i+2],inner[i+1],NULL,NULL);
		hold->e1->f2 |= EDGEINNER;
		hold->e3->f2 |= EDGEINNER;
		facecopy(em, efa,hold);

		hold = addfacelist(em, inner[i],inner[i+1],verts[0][numcuts-1-i],verts[0][numcuts-i],NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		hold->e4->f2 |= EDGEINNER;
		facecopy(em, efa,hold);

		//if(scene->toolsettings->editbutflag & B_AUTOFGON) {
		//	hold->e1->h |= EM_FGON;
		//}
	}

	//EM_fgon_flags(em);

	MEM_freeN(inner);
}

static void fill_tri_double(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[2]={NULL, NULL};
	EditVert *v[3], **verts[2];
	EditFace *hold;
	short start=0, start2=0, vertsize,i;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;

	if(efa->e1->f & SELECT && efa->e2->f & SELECT) {cedge[0] = efa->e1;  cedge[1] = efa->e2; start = 0; start2 = 1;}
	if(efa->e2->f & SELECT && efa->e3->f & SELECT) {cedge[0] = efa->e2;  cedge[1] = efa->e3; start = 1; start2 = 2;}
	if(efa->e3->f & SELECT && efa->e1->f & SELECT) {cedge[0] = efa->e3;  cedge[1] = efa->e1; start = 2; start2 = 0;}

	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != v[start2]) {flipvertarray(verts[1],numcuts+2);}
	/*
	We should have something like this now

			   end		 start
				3   2   1   0
		start2 0|---*---*---|
				|		 /
			   1*	   /
				|	 /
			   2*   /
				| /
		 end2  3|

	We will fill this case like this or this depending on even or odd cuts
			   |---*---*---|
			   | /   /   /
			   *   /   /
			   | /   /
			   *   /
			   | /
			   |
	*/

	// Make outside tri
	hold = addfacelist(em, verts[0][vertsize-2],verts[0][vertsize-1],verts[1][1],NULL,NULL,NULL);
	hold->e3->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	// Make side faces

	for(i=0;i<numcuts;i++) {
		hold = addfacelist(em, verts[0][i],verts[0][i+1],verts[1][vertsize-1-(i+1)],verts[1][vertsize-1-i],NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
	}
}

static void fill_quad_triple(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts)
{
	EditEdge *cedge[3]={0};
	EditVert *v[4], **verts[3];
	EditFace *hold;
	short start=0, start2=0, start3=0, vertsize, i, repeats;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(!(efa->e1->f & SELECT)) {
		cedge[0] = efa->e2;
		cedge[1] = efa->e3;
		cedge[2] = efa->e4;
		start = 1;start2 = 2;start3 = 3;
	}
	if(!(efa->e2->f & SELECT)) {
		cedge[0] = efa->e3;
		cedge[1] = efa->e4;
		cedge[2] = efa->e1;
		start = 2;start2 = 3;start3 = 0;
	}
	if(!(efa->e3->f & SELECT)) {
		cedge[0] = efa->e4;
		cedge[1] = efa->e1;
		cedge[2] = efa->e2;
		start = 3;start2 = 0;start3 = 1;
	}
	if(!(efa->e4->f & SELECT)) {
		cedge[0] = efa->e1;
		cedge[1] = efa->e2;
		cedge[2] = efa->e3;
		start = 0;start2 = 1;start3 = 2;
	}
	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, cedge[0]);
	verts[1] = BLI_ghash_lookup(gh, cedge[1]);
	verts[2] = BLI_ghash_lookup(gh, cedge[2]);
	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != v[start]) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != v[start2]) {flipvertarray(verts[1],numcuts+2);}
	if(verts[2][0] != v[start3]) {flipvertarray(verts[2],numcuts+2);}
	/*
	 We should have something like this now

	 start2
	 3   2   1   0
	 start3 0|---*---*---|3
	 |		   |
	 1*		   *2
	 |		   |
	 2*		   *1
	 |		   |
	 3|-----------|0 start

	 We will fill this case like this or this depending on even or odd cuts
	 there are a couple of differences. For odd cuts, there is a tri in the
	 middle as well as 1 quad at the bottom (not including the extra quads
	 for odd cuts > 1

	 For even cuts, there is a quad in the middle and 2 quads on the bottom

	 they are numbered here for clarity

	 1 outer tris and bottom quads
	 2 inner tri or quad
	 3 repeating quads

	 |---*---*---*---|
	 |1/   /  \   \ 1|
	 |/ 3 /	\  3 \|
	 *  /	2   \   *
	 | /		  \  |
	 |/			\ |
	 *---------------*
	 |	  3		|
	 |			   |
	 *---------------*
	 |			   |
	 |	  1		|
	 |			   |
	 |---------------|

	 |---*---*---*---*---|
	 | 1/   /	 \   \ 1|
	 | /   /	   \   \ |
	 |/ 3 /		 \ 3 \|
	 *   /		   \   *
	 |  /			 \  |
	 | /	   2	   \ |
	 |/				 \|
	 *-------------------*
	 |				   |
	 |		 3		 |
	 |				   |
	 *-------------------*
	 |				   |
	 |		 1		 |
	 |				   |
	 *-------------------*
	 |				   |
	 |		1		  |
	 |				   |
	 |-------------------|

	 */

	// Make outside tris
	hold = addfacelist(em, verts[0][vertsize-2],verts[0][vertsize-1],verts[1][1],NULL,NULL,NULL);
	hold->e3->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	hold = addfacelist(em, verts[1][vertsize-2],verts[1][vertsize-1],verts[2][1],NULL,NULL,NULL);
	hold->e3->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	// Make bottom quad
	hold = addfacelist(em, verts[0][0],verts[0][1],verts[2][vertsize-2],verts[2][vertsize-1],NULL,NULL);
	hold->e2->f2 |= EDGEINNER;
	facecopy(em, efa,hold);
	//If it is even cuts, add the 2nd lower quad
	if(numcuts % 2 == 0) {
		hold = addfacelist(em, verts[0][1],verts[0][2],verts[2][vertsize-3],verts[2][vertsize-2],NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
		// Also Make inner quad
		hold = addfacelist(em, verts[1][numcuts/2],verts[1][(numcuts/2)+1],verts[2][numcuts/2],verts[0][(numcuts/2)+1],NULL,NULL);
		hold->e3->f2 |= EDGEINNER;
		//if(scene->toolsettings->editbutflag & B_AUTOFGON) {
		//	hold->e3->h |= EM_FGON;
		//}
		facecopy(em, efa,hold);
		repeats = (numcuts / 2) -1;
	} else {
		// Make inner tri
		hold = addfacelist(em, verts[1][(numcuts/2)+1],verts[2][(numcuts/2)+1],verts[0][(numcuts/2)+1],NULL,NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		//if(scene->toolsettings->editbutflag & B_AUTOFGON) {
		//	hold->e2->h |= EM_FGON;
		//}
		facecopy(em, efa,hold);
		repeats = ((numcuts+1) / 2)-1;
	}

	// cuts for 1 and 2 do not have the repeating quads
	if(numcuts < 3) {repeats = 0;}
	for(i=0;i<repeats;i++) {
		//Make side repeating Quads
		hold = addfacelist(em, verts[1][i+1],verts[1][i+2],verts[0][vertsize-i-3],verts[0][vertsize-i-2],NULL,NULL);
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
		hold = addfacelist(em, verts[1][vertsize-i-3],verts[1][vertsize-i-2],verts[2][i+1],verts[2][i+2],NULL,NULL);
		hold->e4->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
	}
	// Do repeating bottom quads
	for(i=0;i<repeats;i++) {
		if(numcuts % 2 == 1) {
			hold = addfacelist(em, verts[0][1+i],verts[0][2+i],verts[2][vertsize-3-i],verts[2][vertsize-2-i],NULL,NULL);
		} else {
			hold = addfacelist(em, verts[0][2+i],verts[0][3+i],verts[2][vertsize-4-i],verts[2][vertsize-3-i],NULL,NULL);
		}
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa,hold);
	}
	//EM_fgon_flags(em);
}

static void fill_quad_quadruple(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts, float smooth, float fractal, int beauty)
{
	EditVert **verts[4], ***innerverts;
	EditFace *hold;
	EditEdge temp;
	short vertsize, i, j;

	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, efa->e1);
	verts[1] = BLI_ghash_lookup(gh, efa->e2);
	verts[2] = BLI_ghash_lookup(gh, efa->e3);
	verts[3] = BLI_ghash_lookup(gh, efa->e4);

	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != efa->v1) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != efa->v2) {flipvertarray(verts[1],numcuts+2);}
	if(verts[2][0] == efa->v3) {flipvertarray(verts[2],numcuts+2);}
	if(verts[3][0] == efa->v4) {flipvertarray(verts[3],numcuts+2);}
	/*
	We should have something like this now
					  1

				3   2   1   0
			   0|---*---*---|0
				|           |
			   1*           *1
		     2  |           |   4
			   2*           *2
				|           |
			   3|---*---*---|3
				3   2   1   0

					  3
	// we will fill a 2 dim array of editvert*s to make filling easier
	//  the innervert order is shown

				0   0---1---2---3
					|   |   |   |
				1   0---1---2---3
					|   |   |   |
				2   0---1---2---3
					|   |   |   |
				3   0---1---2---3

	 */
	innerverts = MEM_mallocN(sizeof(EditVert*)*(numcuts+2),"quad-quad subdiv inner verts outer array");
	for(i=0;i<numcuts+2;i++) {
		innerverts[i] = MEM_mallocN(sizeof(EditVert*)*(numcuts+2),"quad-quad subdiv inner verts inner array");
	}

	// first row is e1 last row is e3
	for(i=0;i<numcuts+2;i++) {
		innerverts[0][i]		  = verts[0][(numcuts+1)-i];
		innerverts[numcuts+1][i]  = verts[2][(numcuts+1)-i];
	}

	for(i=1;i<=numcuts;i++) {
		/* we create a fake edge for the next loop */
		temp.v2 = innerverts[i][0] = verts[1][i];
		temp.v1 = innerverts[i][numcuts+1]  = verts[3][i];

		for(j=1;j<=numcuts;j++) {
			float percent= (float)j/(float)(numcuts+1);

			innerverts[i][(numcuts+1)-j]= subdivide_edge_addvert(em, &temp, smooth, fractal, beauty, percent);
		}
	}
	// Fill with faces
	for(i=0;i<numcuts+1;i++) {
		for(j=0;j<numcuts+1;j++) {
			hold = addfacelist(em, innerverts[i][j+1],innerverts[i][j],innerverts[i+1][j],innerverts[i+1][j+1],NULL,NULL);
			hold->e1->f2 = EDGENEW;
			hold->e2->f2 = EDGENEW;
			hold->e3->f2 = EDGENEW;
			hold->e4->f2 = EDGENEW;

			if(i != 0) { hold->e1->f2 |= EDGEINNER; }
			if(j != 0) { hold->e2->f2 |= EDGEINNER; }
			if(i != numcuts) { hold->e3->f2 |= EDGEINNER; }
			if(j != numcuts) { hold->e4->f2 |= EDGEINNER; }

			facecopy(em, efa,hold);
		}
	}
	// Clean up our dynamic multi-dim array
	for(i=0;i<numcuts+2;i++) {
	   MEM_freeN(innerverts[i]);
	}
	MEM_freeN(innerverts);
}

static void fill_tri_triple(EditMesh *em, EditFace *efa, struct GHash *gh, int numcuts, float smooth, float fractal, int beauty)
{
	EditVert **verts[3], ***innerverts;
	short vertsize, i, j;
	EditFace *hold;
	EditEdge temp;

	// Point verts[0] and [1] to the array of new verts for cedge[0] and cedge[1]
	verts[0] = BLI_ghash_lookup(gh, efa->e1);
	verts[1] = BLI_ghash_lookup(gh, efa->e2);
	verts[2] = BLI_ghash_lookup(gh, efa->e3);

	//This is the index size of the verts array
	vertsize = numcuts+2;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0][0] != efa->v1) {flipvertarray(verts[0],numcuts+2);}
	if(verts[1][0] != efa->v2) {flipvertarray(verts[1],numcuts+2);}
	if(verts[2][0] != efa->v3) {flipvertarray(verts[2],numcuts+2);}
	/*
	We should have something like this now
					   3

				3   2   1   0
			   0|---*---*---|3
				|		  /
		  1	1*		*2
				|	  /
			   2*	*1	   2
				|  /
			   3|/
				 0

	we will fill a 2 dim array of editvert*s to make filling easier

						3

			 0  0---1---2---3---4
				| / | /  |/  | /
			 1  0---1----2---3
	   1		| /  | / | /
			 2  0----1---2	 2
				|  / |  /
				|/   |/
			 3  0---1
				|  /
				|/
			 4  0

	*/

	innerverts = MEM_mallocN(sizeof(EditVert*)*(numcuts+2),"tri-tri subdiv inner verts outer array");
	for(i=0;i<numcuts+2;i++) {
		  innerverts[i] = MEM_mallocN(sizeof(EditVert*)*((numcuts+2)-i),"tri-tri subdiv inner verts inner array");
	}
	//top row is e3 backwards
	for(i=0;i<numcuts+2;i++) {
		  innerverts[0][i]		  = verts[2][(numcuts+1)-i];
	}

	for(i=1;i<=numcuts+1;i++) {
		//fake edge, first vert is from e1, last is from e2
		temp.v1= innerverts[i][0]			  = verts[0][i];
		temp.v2= innerverts[i][(numcuts+1)-i]  = verts[1][(numcuts+1)-i];

		for(j=1;j<(numcuts+1)-i;j++) {
			float percent= (float)j/(float)((numcuts+1)-i);

			innerverts[i][((numcuts+1)-i)-j]= subdivide_edge_addvert(em, &temp, smooth, fractal, beauty, 1-percent);
		}
	}

	// Now fill the verts with happy little tris :)
	for(i=0;i<=numcuts+1;i++) {
		for(j=0;j<(numcuts+1)-i;j++) {
			//We always do the first tri
			hold = addfacelist(em, innerverts[i][j+1],innerverts[i][j],innerverts[i+1][j],NULL,NULL,NULL);
			hold->e1->f2 |= EDGENEW;
			hold->e2->f2 |= EDGENEW;
			hold->e3->f2 |= EDGENEW;
			if(i != 0) { hold->e1->f2 |= EDGEINNER; }
			if(j != 0) { hold->e2->f2 |= EDGEINNER; }
			if(j+1 != (numcuts+1)-i) {hold->e3->f2 |= EDGEINNER;}

			facecopy(em, efa,hold);
			//if there are more to come, we do the 2nd
			if(j+1 <= numcuts-i) {
				hold = addfacelist(em, innerverts[i+1][j],innerverts[i+1][j+1],innerverts[i][j+1],NULL,NULL,NULL);
				facecopy(em, efa,hold);
				hold->e1->f2 |= EDGENEW;
				hold->e2->f2 |= EDGENEW;
				hold->e3->f2 |= EDGENEW;
			}
		}
	}

	// Clean up our dynamic multi-dim array
	for(i=0;i<numcuts+2;i++) {
		MEM_freeN(innerverts[i]);
	}
	MEM_freeN(innerverts);
}

//Next two fill types are for   exact only and are provided to allow for knifing through vertices
//This means there is no multicut!
static void fill_quad_doublevert(EditMesh *em, EditFace *efa, int v1, int v2)
{
	EditFace *hold;
	/*
		Depending on which two vertices have been knifed through (v1 and v2), we
		triangulate like the patterns below.
				X-------|	|-------X
				| \  	|	|     / |
				|   \	|	|   /	|
				|	  \	|	| /	    |
				--------X	X--------
	*/

	if(v1 == 1 && v2 == 3){
		hold= addfacelist(em, efa->v1, efa->v2, efa->v3, 0, efa, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e3->f2 |= EDGEINNER;
		facecopy(em, efa, hold);

		hold= addfacelist(em, efa->v1, efa->v3, efa->v4, 0, efa, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e1->f2 |= EDGEINNER;
		facecopy(em, efa, hold);
	}
	else{
		hold= addfacelist(em, efa->v1, efa->v2, efa->v4, 0, efa, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e2->f2 |= EDGEINNER;
		facecopy(em, efa, hold);

		hold= addfacelist(em, efa->v2, efa->v3, efa->v4, 0, efa, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e3->f2 |= EDGEINNER;
		facecopy(em, efa, hold);
	}
}

static void fill_quad_singlevert(EditMesh *em, EditFace *efa, struct GHash *gh)
{
	EditEdge *cedge=NULL;
	EditVert *v[4], **verts;
	EditFace *hold;
	short start=0, end, left, right, vertsize;

	v[0] = efa->v1;
	v[1] = efa->v2;
	v[2] = efa->v3;
	v[3] = efa->v4;

	if(efa->e1->f & SELECT)	  { cedge = efa->e1; start = 0;}
	else if(efa->e2->f & SELECT) { cedge = efa->e2; start = 1;}
	else if(efa->e3->f & SELECT) { cedge = efa->e3; start = 2;}
	else if(efa->e4->f & SELECT) { cedge = efa->e4; start = 3;}

	// Point verts to the array of new verts for cedge
	verts = BLI_ghash_lookup(gh, cedge);
	//This is the index size of the verts array
	vertsize = 3;

	// Is the original v1 the same as the first vert on the selected edge?
	// if not, the edge is running the opposite direction in this face so flip
	// the array to the correct direction

	if(verts[0] != v[start]) {flipvertarray(verts,3);}
	end	= (start+1)%4;
	left   = (start+2)%4;
	right  = (start+3)%4;

/*
	We should have something like this now

			  end		 start
			   2     1     0
			   |-----*-----|
			   |		   |
			   |		   |
			   |		   |
			   -------------
			  left	   right

	where start,end,left, right are indexes of EditFace->v1, etc (stored in v)
	and 0,1,2 are the indexes of the new verts stored in verts. We fill like
	this, depending on whether its vertex 'left' or vertex 'right' thats
	been knifed through...

				|---*---|	|---*---|
				|  /	|	|    \  |
				| /		|	|	  \ |
				|/		|	|	   \|
				X--------	--------X
*/

	if(v[left]->f1){
		//triangle is composed of cutvert, end and left
		hold = addfacelist(em, verts[1],v[end],v[left],NULL, NULL,NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e3->f2 |= EDGEINNER;
		facecopy(em, efa, hold);

		//quad is composed of cutvert, left, right and start
		hold = addfacelist(em, verts[1],v[left],v[right],v[start], NULL, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e4->f2 |= EDGENEW;
		hold->e1->f2 |= EDGEINNER;
		facecopy(em, efa, hold);
	}
	else if(v[right]->f1){
		//triangle is composed of cutvert, right and start
		hold = addfacelist(em, verts[1],v[right],v[start], NULL, NULL, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e1->f2 |= EDGEINNER;
		facecopy(em, efa, hold);
		//quad is composed of cutvert, end, left, right
		hold = addfacelist(em, verts[1],v[end], v[left], v[right], NULL, NULL);
		hold->e1->f2 |= EDGENEW;
		hold->e2->f2 |= EDGENEW;
		hold->e3->f2 |= EDGENEW;
		hold->e4->f2 |= EDGENEW;
		hold->e4->f2 |= EDGEINNER;
		facecopy(em, efa, hold);
	}

}

// This function takes an example edge, the current point to create and
// the total # of points to create, then creates the point and return the
// editvert pointer to it.
static EditVert *subdivideedgenum(EditMesh *em, EditEdge *edge, int curpoint, int totpoint, float smooth, float fractal, int beauty)
{
	EditVert *ev;
	float percent;

	if (beauty & (B_PERCENTSUBD) && totpoint == 1)
		//percent=(float)(edge->tmp.l)/32768.0f;
		percent= edge->tmp.fp;
	else
		percent= (float)curpoint/(float)(totpoint+1);

	ev= subdivide_edge_addvert(em, edge, smooth, fractal, beauty, percent);
	ev->f = edge->v1->f;

	return ev;
}

void esubdivideflag(Object *obedit, EditMesh *em, int flag, float smooth, float fractal, int beauty, int numcuts, int seltype)
{
	EditFace *ef;
	EditEdge *eed, *cedge, *sort[4];
	EditVert *eve, **templist;
	struct GHash *gh;
	float length[4], v1mat[3], v2mat[3], v3mat[3], v4mat[3];
	int i, j, edgecount, touchcount, facetype,hold;
	ModifierData *md= obedit->modifiers.first;
	int ctrl= 0; // XXX

	//Set faces f1 to 0 cause we need it later
	for(ef=em->faces.first;ef;ef = ef->next) ef->f1 = 0;
	for(eve=em->verts.first; eve; eve=eve->next) {
		if(!(beauty & B_KNIFE)) /* knife sets this flag for vertex cuts */
			eve->f1 = 0;
		eve->f2 = 0;
	}

	for (; md; md=md->next) {
		if (md->type==eModifierType_Mirror) {
			MirrorModifierData *mmd = (MirrorModifierData*) md;

			if(mmd->flag & MOD_MIR_CLIPPING) {
				for (eve= em->verts.first; eve; eve= eve->next) {
					eve->f2= 0;
					switch(mmd->axis){
						case 0:
							if (fabs(eve->co[0]) < mmd->tolerance)
								eve->f2 |= 1;
							break;
						case 1:
							if (fabs(eve->co[1]) < mmd->tolerance)
								eve->f2 |= 2;
							break;
						case 2:
							if (fabs(eve->co[2]) < mmd->tolerance)
								eve->f2 |= 4;
							break;
					}
				}
			}
		}
	}

	//Flush vertex flags upward to the edges
	for(eed = em->edges.first;eed;eed = eed->next) {
		//if(eed->f & flag && eed->v1->f == eed->v2->f) {
		//	eed->f |= eed->v1->f;
		// }
		eed->f2 = 0;
		if(eed->f & flag) {
			eed->f2	|= EDGEOLD;
		}
	}

	// We store an array of verts for each edge that is subdivided,
	// we put this array as a value in a ghash which is keyed by the EditEdge*

	// Now for beauty subdivide deselect edges based on length
	if(beauty & B_BEAUTY) {
		for(ef = em->faces.first;ef;ef = ef->next) {
			if(!ef->v4) {
				continue;
			}
			if(ef->f & SELECT) {
				VECCOPY(v1mat, ef->v1->co);
				VECCOPY(v2mat, ef->v2->co);
				VECCOPY(v3mat, ef->v3->co);
				VECCOPY(v4mat, ef->v4->co);
				Mat4Mul3Vecfl(obedit->obmat, v1mat);
				Mat4Mul3Vecfl(obedit->obmat, v2mat);
				Mat4Mul3Vecfl(obedit->obmat, v3mat);
				Mat4Mul3Vecfl(obedit->obmat, v4mat);

				length[0] = VecLenf(v1mat, v2mat);
				length[1] = VecLenf(v2mat, v3mat);
				length[2] = VecLenf(v3mat, v4mat);
				length[3] = VecLenf(v4mat, v1mat);
				sort[0] = ef->e1;
				sort[1] = ef->e2;
				sort[2] = ef->e3;
				sort[3] = ef->e4;


				// Beauty Short Edges
				if(beauty & B_BEAUTY_SHORT) {
					for(j=0;j<2;j++) {
						hold = -1;
						for(i=0;i<4;i++) {
							if(length[i] < 0) {
								continue;
							} else if(hold == -1) {
								hold = i;
							} else {
								if(length[hold] < length[i]) {
									hold = i;
								}
							}
						}
						if (hold > -1) {
							sort[hold]->f &= ~SELECT;
							sort[hold]->f2 |= EDGENEW;
							length[hold] = -1;
						}
					}
				}

				// Beauty Long Edges
				else {
					 for(j=0;j<2;j++) {
						hold = -1;
						for(i=0;i<4;i++) {
							if(length[i] < 0) {
								continue;
							} else if(hold == -1) {
								hold = i;
							} else {
								if(length[hold] > length[i]) {
									hold = i;
								}
							}
						}
						if (hold > -1) {
							sort[hold]->f &= ~SELECT;
							sort[hold]->f2 |= EDGENEW;
							length[hold] = -1;
						}
					}
				}
			}
		}
	}

	gh = BLI_ghash_new(BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp);

	// If we are knifing, We only need the selected edges that were cut, so deselect if it was not cut
	if(beauty & B_KNIFE) {
		for(eed= em->edges.first;eed;eed=eed->next) {
			if( eed->tmp.fp == 0 ) {
				EM_select_edge(eed,0);
			}
		}
	}
	// So for each edge, if it is selected, we allocate an array of size cuts+2
	// so we can have a place for the v1, the new verts and v2
	for(eed=em->edges.first;eed;eed = eed->next) {
		if(eed->f & flag) {
			templist = MEM_mallocN(sizeof(EditVert*)*(numcuts+2),"vertlist");
			templist[0] = eed->v1;
			for(i=0;i<numcuts;i++) {
				// This function creates the new vert and returns it back
				// to the array
				templist[i+1] = subdivideedgenum(em, eed, i+1, numcuts, smooth, fractal, beauty);
				//while we are here, we can copy edge info from the original edge
				cedge = addedgelist(em, templist[i],templist[i+1],eed);
				// Also set the edge f2 to EDGENEW so that we can use this info later
				cedge->f2 = EDGENEW;
			}
			templist[i+1] = eed->v2;
			//Do the last edge too
			cedge = addedgelist(em, templist[i],templist[i+1],eed);
			cedge->f2 = EDGENEW;
			// Now that the edge is subdivided, we can put its verts in the ghash
			BLI_ghash_insert(gh, eed, templist);
		}
	}

//	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	// Now for each face in the mesh we need to figure out How many edges were cut
	// and which filling method to use for that face
	for(ef = em->faces.first;ef;ef = ef->next) {
		edgecount = 0;
		facetype = 3;
		if(ef->e1->f & flag) {edgecount++;}
		if(ef->e2->f & flag) {edgecount++;}
		if(ef->e3->f & flag) {edgecount++;}
		if(ef->v4) {
			facetype = 4;
			if(ef->e4->f & flag) {edgecount++;}
		}
		if(facetype == 4) {
			switch(edgecount) {
				case 0:
					if(beauty & B_KNIFE && numcuts == 1){
						/*Test for when knifing through two opposite verts but no edges*/
						touchcount = 0;
						if(ef->v1->f1) touchcount++;
						if(ef->v2->f1) touchcount++;
						if(ef->v3->f1) touchcount++;
						if(ef->v4->f1) touchcount++;
						if(touchcount == 2){
							if(ef->v1->f1 && ef->v3->f1){
								ef->f1 = SELECT;
								fill_quad_doublevert(em, ef, 1, 3);
							}
							else if(ef->v2->f1 && ef->v4->f1){
								ef->f1 = SELECT;
								fill_quad_doublevert(em, ef, 2, 4);
							}
						}
					}
					break;

				case 1:
					if(beauty & B_KNIFE && numcuts == 1){
						/*Test for when knifing through an edge and one vert*/
						touchcount = 0;
						if(ef->v1->f1) touchcount++;
						if(ef->v2->f1) touchcount++;
						if(ef->v3->f1) touchcount++;
						if(ef->v4->f1) touchcount++;

						if(touchcount == 1){
							if( (ef->e1->f & flag && ( !ef->e1->v1->f1 && !ef->e1->v2->f1 )) ||
								(ef->e2->f & flag && ( !ef->e2->v1->f1 && !ef->e2->v2->f1 )) ||
								(ef->e3->f & flag && ( !ef->e3->v1->f1 && !ef->e3->v2->f1 )) ||
								(ef->e4->f & flag && ( !ef->e4->v1->f1 && !ef->e4->v2->f1 )) ){

								ef->f1 = SELECT;
								fill_quad_singlevert(em, ef, gh);
							}
							else{
								ef->f1 = SELECT;
								fill_quad_single(em, ef, gh, numcuts, seltype);
							}
						}
						else{
							ef->f1 = SELECT;
							fill_quad_single(em, ef, gh, numcuts, seltype);
						}
					}
					else{
						ef->f1 = SELECT;
						fill_quad_single(em, ef, gh, numcuts, seltype);
					}
					break;
				case 2: ef->f1 = SELECT;
					// if there are 2, we check if edge 1 and 3 are either both on or off that way
					// we can tell if the selected pair is Adjacent or Opposite of each other
					if((ef->e1->f & flag && ef->e3->f & flag) ||
					   (ef->e2->f & flag && ef->e4->f & flag)) {
						fill_quad_double_op(em, ef, gh, numcuts);
					}else{
						switch(0) { // XXX scene->toolsettings->cornertype) {
							case 0:	fill_quad_double_adj_path(em, ef, gh, numcuts); break;
							case 1:	fill_quad_double_adj_inner(em, ef, gh, numcuts); break;
							case 2:	fill_quad_double_adj_fan(em, ef, gh, numcuts); break;
						}

					}
						break;
				case 3: ef->f1 = SELECT;
					fill_quad_triple(em, ef, gh, numcuts);
					break;
				case 4: ef->f1 = SELECT;
					fill_quad_quadruple(em, ef, gh, numcuts, smooth, fractal, beauty);
					break;
			}
		} else {
			switch(edgecount) {
				case 0: break;
				case 1: ef->f1 = SELECT;
					fill_tri_single(em, ef, gh, numcuts, seltype);
					break;
				case 2: ef->f1 = SELECT;
					fill_tri_double(em, ef, gh, numcuts);
					break;
				case 3: ef->f1 = SELECT;
					fill_tri_triple(em, ef, gh, numcuts, smooth, fractal, beauty);
					break;
			}
		}
	}

	// Delete Old Edges and Faces
	for(eed = em->edges.first;eed;eed = eed->next) {
		if(BLI_ghash_haskey(gh,eed)) {
			eed->f1 = SELECT;
		} else {
			eed->f1 = 0;
		}
	}
	free_tagged_edges_faces(em, em->edges.first, em->faces.first);

	if(seltype == SUBDIV_SELECT_ORIG  && !ctrl) {
		/* bugfix: vertex could get flagged as "not-selected"
		// solution: clear flags before, not at the same time as setting SELECT flag -dg
		*/
		for(eed = em->edges.first;eed;eed = eed->next) {
			if(!(eed->f2 & EDGENEW || eed->f2 & EDGEOLD)) {
				eed->f &= !flag;
				EM_select_edge(eed,0);
			}
		}
		for(eed = em->edges.first;eed;eed = eed->next) {
			if(eed->f2 & EDGENEW || eed->f2 & EDGEOLD) {
				eed->f |= flag;
				EM_select_edge(eed,1);
			}
		}
	} else if ((seltype == SUBDIV_SELECT_INNER || seltype == SUBDIV_SELECT_INNER_SEL)|| ctrl) {
		for(eed = em->edges.first;eed;eed = eed->next) {
			if(eed->f2 & EDGEINNER) {
				eed->f |= flag;
				EM_select_edge(eed,1);
				if(eed->v1->f & EDGEINNER) eed->v1->f |= SELECT;
				if(eed->v2->f & EDGEINNER) eed->v2->f |= SELECT;
			}else{
				eed->f &= !flag;
				EM_select_edge(eed,0);
			}
		}
	} else if(seltype == SUBDIV_SELECT_LOOPCUT){
		for(eed = em->edges.first;eed;eed = eed->next) {
			if(eed->f2 & DOUBLEOPFILL){
				eed->f |= flag;
				EM_select_edge(eed,1);
			}else{
				eed->f &= !flag;
				EM_select_edge(eed,0);
			}
		}
	}
	 if(em->selectmode & SCE_SELECT_VERTEX) {
		 for(eed = em->edges.first;eed;eed = eed->next) {
			if(eed->f & SELECT) {
				eed->v1->f |= SELECT;
				eed->v2->f |= SELECT;
			}
		}
	}

	//fix hide flags for edges. First pass, hide edges of hidden faces
	for(ef=em->faces.first; ef; ef=ef->next){
		if(ef->h){
			ef->e1->h |= 1;
			ef->e2->h |= 1;
			ef->e3->h |= 1;
			if(ef->e4) ef->e4->h |= 1;
		}
	}
	//second pass: unhide edges of visible faces adjacent to hidden faces
	for(ef=em->faces.first; ef; ef=ef->next){
		if(ef->h == 0){
			ef->e1->h &= ~1;
			ef->e2->h &= ~1;
			ef->e3->h &= ~1;
			if(ef->e4) ef->e4->h &= ~1;
		}
	}

	// Free the ghash and call MEM_freeN on all the value entries to return
	// that memory
	BLI_ghash_free(gh, NULL, (GHashValFreeFP)MEM_freeN);

	EM_selectmode_flush(em);
	for(ef=em->faces.first;ef;ef = ef->next) {
		if(ef->e4) {
			if(  (ef->e1->f & SELECT && ef->e2->f & SELECT) &&
			 (ef->e3->f & SELECT && ef->e4->f & SELECT) ) {
				ef->f |= SELECT;
			}
		} else {
			if(  (ef->e1->f & SELECT && ef->e2->f & SELECT) && ef->e3->f & SELECT) {
				ef->f |= SELECT;
			}
		}
	}

	recalc_editnormals(em);
}

static int count_selected_edges(EditEdge *ed)
{
	int totedge = 0;
	while(ed) {
		ed->tmp.p = 0;
		if( ed->f & SELECT ) totedge++;
		ed= ed->next;
	}
	return totedge;
}

/* hurms, as if this makes code readable! It's pointerpointer hiding... (ton) */
typedef EditFace *EVPtr;
typedef EVPtr EVPTuple[2];

/** builds EVPTuple array efaa of face tuples (in fact pointers to EditFaces)
	sharing one edge.
	arguments: selected edge list, face list.
	Edges will also be tagged accordingly (see eed->f2)		  */

static int collect_quadedges(EVPTuple *efaa, EditEdge *eed, EditFace *efa)
{
	EditEdge *e1, *e2, *e3;
	EVPtr *evp;
	int i = 0;

	/* run through edges, if selected, set pointer edge-> facearray */
	while(eed) {
		eed->f2= 0;
		eed->f1= 0;
		if( eed->f & SELECT ) {
			eed->tmp.p = (EditVert *) (&efaa[i]);
			i++;
		}
		else eed->tmp.p = NULL;

		eed= eed->next;
	}


	/* find edges pointing to 2 faces by procedure:

	- run through faces and their edges, increase
	  face counter e->f1 for each face
	*/

	while(efa) {
		efa->f1= 0;
		if(efa->v4==0 && (efa->f & SELECT)) {  /* if selected triangle */
			e1= efa->e1;
			e2= efa->e2;
			e3= efa->e3;
			if(e1->f2<3 && e1->tmp.p) {
				if(e1->f2<2) {
					evp= (EVPtr *) e1->tmp.p;
					evp[(int)e1->f2] = efa;
				}
				e1->f2+= 1;
			}
			if(e2->f2<3 && e2->tmp.p) {
				if(e2->f2<2) {
					evp= (EVPtr *) e2->tmp.p;
					evp[(int)e2->f2]= efa;
				}
				e2->f2+= 1;
			}
			if(e3->f2<3 && e3->tmp.p) {
				if(e3->f2<2) {
					evp= (EVPtr *) e3->tmp.p;
					evp[(int)e3->f2]= efa;
				}
				e3->f2+= 1;
			}
		}
		else {
			/* set to 3 to make sure these are not flipped or joined */
			efa->e1->f2= 3;
			efa->e2->f2= 3;
			efa->e3->f2= 3;
			if (efa->e4) efa->e4->f2= 3;
		}

		efa= efa->next;
	}
	return i;
}


/* returns vertices of two adjacent triangles forming a quad
   - can be righthand or lefthand

			4-----3
			|\	|
			| \ 2 | <- efa1
			|  \  |
	  efa-> | 1 \ |
			|	\|
			1-----2

*/
#define VTEST(face, num, other) \
	(face->v##num != other->v1 && face->v##num != other->v2 && face->v##num != other->v3)

static void givequadverts(EditFace *efa, EditFace *efa1, EditVert **v1, EditVert **v2, EditVert **v3, EditVert **v4, int *vindex)
{
	if VTEST(efa, 1, efa1) {
		*v1= efa->v1;
		*v2= efa->v2;
		vindex[0]= 0;
		vindex[1]= 1;
	}
	else if VTEST(efa, 2, efa1) {
		*v1= efa->v2;
		*v2= efa->v3;
		vindex[0]= 1;
		vindex[1]= 2;
	}
	else if VTEST(efa, 3, efa1) {
		*v1= efa->v3;
		*v2= efa->v1;
		vindex[0]= 2;
		vindex[1]= 0;
	}

	if VTEST(efa1, 1, efa) {
		*v3= efa1->v1;
		*v4= efa1->v2;
		vindex[2]= 0;
		vindex[3]= 1;
	}
	else if VTEST(efa1, 2, efa) {
		*v3= efa1->v2;
		*v4= efa1->v3;
		vindex[2]= 1;
		vindex[3]= 2;
	}
	else if VTEST(efa1, 3, efa) {
		*v3= efa1->v3;
		*v4= efa1->v1;
		vindex[2]= 2;
		vindex[3]= 0;
	}
	else
		*v3= *v4= NULL;
}

/* Helper functions for edge/quad edit features*/
static void untag_edges(EditFace *f)
{
	f->e1->f1 = 0;
	f->e2->f1 = 0;
	f->e3->f1 = 0;
	if (f->e4) f->e4->f1 = 0;
}

/** remove and free list of tagged edges and faces */
static void free_tagged_edges_faces(EditMesh *em, EditEdge *eed, EditFace *efa)
{
	EditEdge *nexted;
	EditFace *nextvl;

	while(efa) {
		nextvl= efa->next;
		if(efa->f1) {
			BLI_remlink(&em->faces, efa);
			free_editface(em, efa);
		}
		else
			/* avoid deleting edges that are still in use */
			untag_edges(efa);
		efa= nextvl;
	}

	while(eed) {
		nexted= eed->next;
		if(eed->f1) {
			remedge(em, eed);
			free_editedge(em, eed);
		}
		eed= nexted;
	}
}


/* ******************** BEGIN TRIANGLE TO QUAD ************************************* */
static float measure_facepair(EditVert *v1, EditVert *v2, EditVert *v3, EditVert *v4, float limit)
{

	/*gives a 'weight' to a pair of triangles that join an edge to decide how good a join they would make*/
	/*Note: this is more complicated than it needs to be and should be cleaned up...*/
	float	measure = 0.0, noA1[3], noA2[3], noB1[3], noB2[3], normalADiff, normalBDiff,
			edgeVec1[3], edgeVec2[3], edgeVec3[3], edgeVec4[3], diff,
			minarea, maxarea, areaA, areaB;

	/*First Test: Normal difference*/
	CalcNormFloat(v1->co, v2->co, v3->co, noA1);
	CalcNormFloat(v1->co, v3->co, v4->co, noA2);

	if(noA1[0] == noA2[0] && noA1[1] == noA2[1] && noA1[2] == noA2[2]) normalADiff = 0.0;
	else normalADiff = VecAngle2(noA1, noA2);
		//if(!normalADiff) normalADiff = 179;
	CalcNormFloat(v2->co, v3->co, v4->co, noB1);
	CalcNormFloat(v4->co, v1->co, v2->co, noB2);

	if(noB1[0] == noB2[0] && noB1[1] == noB2[1] && noB1[2] == noB2[2]) normalBDiff = 0.0;
	else normalBDiff = VecAngle2(noB1, noB2);
		//if(!normalBDiff) normalBDiff = 179;

	measure += (normalADiff/360) + (normalBDiff/360);
	if(measure > limit) return measure;

	/*Second test: Colinearity*/
	VecSubf(edgeVec1, v1->co, v2->co);
	VecSubf(edgeVec2, v2->co, v3->co);
	VecSubf(edgeVec3, v3->co, v4->co);
	VecSubf(edgeVec4, v4->co, v1->co);

	diff = 0.0;

	diff = (
		fabs(VecAngle2(edgeVec1, edgeVec2) - 90) +
		fabs(VecAngle2(edgeVec2, edgeVec3) - 90) +
		fabs(VecAngle2(edgeVec3, edgeVec4) - 90) +
		fabs(VecAngle2(edgeVec4, edgeVec1) - 90)) / 360;
	if(!diff) return 0.0;

	measure +=  diff;
	if(measure > limit) return measure;

	/*Third test: Concavity*/
	areaA = AreaT3Dfl(v1->co, v2->co, v3->co) + AreaT3Dfl(v1->co, v3->co, v4->co);
	areaB = AreaT3Dfl(v2->co, v3->co, v4->co) + AreaT3Dfl(v4->co, v1->co, v2->co);

	if(areaA <= areaB) minarea = areaA;
	else minarea = areaB;

	if(areaA >= areaB) maxarea = areaA;
	else maxarea = areaB;

	if(!maxarea) measure += 1;
	else measure += (1 - (minarea / maxarea));

	return measure;
}

#define T2QUV_LIMIT 0.005
#define T2QCOL_LIMIT 3
static int compareFaceAttribs(EditMesh *em, EditFace *f1, EditFace *f2, EditEdge *eed)
{
	/*Test to see if the per-face attributes for the joining edge match within limit*/
	MTFace *tf1, *tf2;
	unsigned int *col1, *col2;
	short i,attrok=0, flag = 0, /* XXX scene->toolsettings->editbutflag,*/ fe1[2], fe2[2];

	tf1 = CustomData_em_get(&em->fdata, f1->data, CD_MTFACE);
	tf2 = CustomData_em_get(&em->fdata, f2->data, CD_MTFACE);

	col1 = CustomData_em_get(&em->fdata, f1->data, CD_MCOL);
	col2 = CustomData_em_get(&em->fdata, f2->data, CD_MCOL);

	/*store indices for faceedges*/
	f1->v1->f1 = 0;
	f1->v2->f1 = 1;
	f1->v3->f1 = 2;

	fe1[0] = eed->v1->f1;
	fe1[1] = eed->v2->f1;

	f2->v1->f1 = 0;
	f2->v2->f1 = 1;
	f2->v3->f1 = 2;

	fe2[0] = eed->v1->f1;
	fe2[1] = eed->v2->f1;

	/*compare faceedges for each face attribute. Additional per face attributes can be added later*/
	/*do UVs*/
	if(flag & B_JOINTRIA_UV){

		if(tf1 == NULL || tf2 == NULL) attrok |= B_JOINTRIA_UV;
		else if(tf1->tpage != tf2->tpage); /*do nothing*/
		else{
			for(i = 0; i < 2; i++){
				if(tf1->uv[fe1[i]][0] + T2QUV_LIMIT > tf2->uv[fe2[i]][0] && tf1->uv[fe1[i]][0] - T2QUV_LIMIT < tf2->uv[fe2[i]][0] &&
					tf1->uv[fe1[i]][1] + T2QUV_LIMIT > tf2->uv[fe2[i]][1] && tf1->uv[fe1[i]][1] - T2QUV_LIMIT < tf2->uv[fe2[i]][1]) attrok |= B_JOINTRIA_UV;
			}
		}
	}

	/*do VCOLs*/
	if(flag & B_JOINTRIA_VCOL){
		if(!col1 || !col2) attrok |= B_JOINTRIA_VCOL;
		else{
			char *f1vcol, *f2vcol;
			for(i = 0; i < 2; i++){
				f1vcol = (char *)&(col1[fe1[i]]);
				f2vcol = (char *)&(col2[fe2[i]]);

				/*compare f1vcol with f2vcol*/
				if(	f1vcol[1] + T2QCOL_LIMIT > f2vcol[1] && f1vcol[1] - T2QCOL_LIMIT < f2vcol[1] &&
					f1vcol[2] + T2QCOL_LIMIT > f2vcol[2] && f1vcol[2] - T2QCOL_LIMIT < f2vcol[2] &&
					f1vcol[3] + T2QCOL_LIMIT > f2vcol[3] && f1vcol[3] - T2QCOL_LIMIT < f2vcol[3]) attrok |= B_JOINTRIA_VCOL;
			}
		}
	}

	if( ((attrok & B_JOINTRIA_UV) == (flag & B_JOINTRIA_UV)) && ((attrok & B_JOINTRIA_VCOL) == (flag & B_JOINTRIA_VCOL)) ) return 1;
	return 0;
}

static int fplcmp(const void *v1, const void *v2)
{
	const EditEdge *e1= *((EditEdge**)v1), *e2=*((EditEdge**)v2);

	if( e1->crease > e2->crease) return 1;
	else if( e1->crease < e2->crease) return -1;

	return 0;
}

/*Bitflags for edges.*/
#define T2QDELETE	1
#define T2QCOMPLEX	2
#define T2QJOIN		4
void join_triangles(EditMesh *em)
{
	EditVert *v1, *v2, *v3, *v4, *eve;
	EditEdge *eed, **edsortblock = NULL, **edb = NULL;
	EditFace *efa;
	EVPTuple *efaar = NULL;
	EVPtr *efaa = NULL;
	float *creases = NULL;
	float measure; /*Used to set tolerance*/
	float limit = 0.0f; // XXX scene->toolsettings->jointrilimit;
	int i, ok, totedge=0, totseledge=0, complexedges, vindex[4];

	/*if we take a long time on very dense meshes we want waitcursor to display*/
	waitcursor(1);

	totseledge = count_selected_edges(em->edges.first);
	if(totseledge==0) return;

	/*abusing crease value to store weights for edge pairs. Nasty*/
	for(eed=em->edges.first; eed; eed=eed->next) totedge++;
	if(totedge) creases = MEM_callocN(sizeof(float) * totedge, "Join Triangles Crease Array");
	for(eed=em->edges.first, i = 0; eed; eed=eed->next, i++){
		creases[i] = eed->crease;
		eed->crease = 0.0;
	}

	/*clear temp flags*/
	for(eve=em->verts.first; eve; eve=eve->next) eve->f1 = eve->f2 = 0;
	for(eed=em->edges.first; eed; eed=eed->next) eed->f2 = eed->f1 = 0;
	for(efa=em->faces.first; efa; efa=efa->next) efa->f1 = efa->tmp.l = 0;

	/*For every selected 2 manifold edge, create pointers to its two faces.*/
	efaar= (EVPTuple *) MEM_callocN(totseledge * sizeof(EVPTuple), "Tri2Quad");
	ok = collect_quadedges(efaar, em->edges.first, em->faces.first);
	complexedges = 0;

	if(ok){


		/*clear tmp.l flag and store number of faces that are selected and coincident to current face here.*/
		for(eed=em->edges.first; eed; eed=eed->next){
			/* eed->f2 is 2 only if this edge is part of exactly two
			   triangles, and both are selected, and it has EVPTuple assigned */
			if(eed->f2 == 2){
				efaa= (EVPtr *) eed->tmp.p;
				efaa[0]->tmp.l++;
				efaa[1]->tmp.l++;
			}
		}

		for(eed=em->edges.first; eed; eed=eed->next){
			if(eed->f2 == 2){
				efaa= (EVPtr *) eed->tmp.p;
				v1 = v2 = v3 = v4 = NULL;
				givequadverts(efaa[0], efaa[1], &v1, &v2, &v3, &v4, vindex);
				if(v1 && v2 && v3 && v4){
					/*test if simple island first. This mimics 2.42 behaviour and the tests are less restrictive.*/
					if(efaa[0]->tmp.l == 1 && efaa[1]->tmp.l == 1){
						eed->f1 |= T2QJOIN;
						efaa[0]->f1 = 1; //mark for join
						efaa[1]->f1 = 1; //mark for join
					}
					else{

						/*	The face pair is part of a 'complex' island, so the rules for dealing with it are more involved.
							Depending on what options the user has chosen, this face pair can be 'thrown out' based upon the following criteria:

							1: the two faces do not share the same material
							2: the edge joining the two faces is marked as sharp.
							3: the two faces UV's do not make a good match
							4: the two faces Vertex colors do not make a good match

							If the face pair passes all the applicable tests, it is then given a 'weight' with the measure_facepair() function.
							This measures things like concavity, colinearity ect. If this weight is below the threshold set by the user
							the edge joining them is marked as being 'complex' and will be compared against other possible pairs which contain one of the
							same faces in the current pair later.

							This technique is based upon an algorithm that Campbell Barton developed for his Tri2Quad script that was previously part of
							the python scripts bundled with Blender releases.
						*/

// XXX						if(scene->toolsettings->editbutflag & B_JOINTRIA_SHARP && eed->sharp); /*do nothing*/
//						else if(scene->toolsettings->editbutflag & B_JOINTRIA_MAT && efaa[0]->mat_nr != efaa[1]->mat_nr); /*do nothing*/
//						else if(((scene->toolsettings->editbutflag & B_JOINTRIA_UV) || (scene->toolsettings->editbutflag & B_JOINTRIA_VCOL)) &&
						compareFaceAttribs(em, efaa[0], efaa[1], eed); // XXX == 0); /*do nothing*/
//						else{
							measure = measure_facepair(v1, v2, v3, v4, limit);
							if(measure < limit){
								complexedges++;
								eed->f1 |= T2QCOMPLEX;
								eed->crease = measure; /*we dont mark edges for join yet*/
							}
//						}
					}
				}
			}
		}

		/*Quicksort the complex edges according to their weighting*/
		if(complexedges){
			edsortblock = edb = MEM_callocN(sizeof(EditEdge*) * complexedges, "Face Pairs quicksort Array");
			for(eed = em->edges.first; eed; eed=eed->next){
				if(eed->f1 & T2QCOMPLEX){
					*edb = eed;
					edb++;
				}
			}
			qsort(edsortblock, complexedges, sizeof(EditEdge*), fplcmp);
			/*now go through and mark the edges who get the highest weighting*/
			for(edb=edsortblock, i=0; i < complexedges; edb++, i++){
				efaa = (EVPtr *)((*edb)->tmp.p); /*suspect!*/
				if( !efaa[0]->f1 && !efaa[1]->f1){
					efaa[0]->f1 = 1; //mark for join
					efaa[1]->f1 = 1; //mark for join
					(*edb)->f1 |= T2QJOIN;
				}
			}
		}

		/*finally go through all edges marked for join (simple and complex) and create new faces*/
		for(eed=em->edges.first; eed; eed=eed->next){
			if(eed->f1 & T2QJOIN){
				efaa= (EVPtr *)eed->tmp.p;
				v1 = v2 = v3 = v4 = NULL;
				givequadverts(efaa[0], efaa[1], &v1, &v2, &v3, &v4, vindex);
				if((v1 && v2 && v3 && v4) && (exist_face(em, v1, v2, v3, v4)==0)){ /*exist_face is very slow! Needs to be adressed.*/
					/*flag for delete*/
					eed->f1 |= T2QDELETE;
					/*create new quad and select*/
					efa = EM_face_from_faces(em, efaa[0], efaa[1], vindex[0], vindex[1], 4+vindex[2], 4+vindex[3]);
					EM_select_face(efa,1);
				}
				else{
						efaa[0]->f1 = 0;
						efaa[1]->f1 = 0;
				}
			}
		}
	}

	/*free data and cleanup*/
	if(creases){
		for(eed=em->edges.first, i = 0; eed; eed=eed->next, i++) eed->crease = creases[i];
		MEM_freeN(creases);
	}
	for(eed=em->edges.first; eed; eed=eed->next){
		if(eed->f1 & T2QDELETE) eed->f1 = 1;
		else eed->f1 = 0;
	}
	free_tagged_edges_faces(em, em->edges.first, em->faces.first);
	if(efaar) MEM_freeN(efaar);
	if(edsortblock) MEM_freeN(edsortblock);

	EM_selectmode_flush(em);

}
/* ******************** END TRIANGLE TO QUAD ************************************* */

#define FACE_MARKCLEAR(f) (f->f1 = 1)

/* quick hack, basically a copy of beauty_fill */
void edge_flip(EditMesh *em)
{
	EditVert *v1, *v2, *v3, *v4;
	EditEdge *eed, *nexted;
	EditFace *efa, *w;
	//void **efaar, **efaa;
	EVPTuple *efaar;
	EVPtr *efaa;
	int totedge, ok, vindex[4];

	/* - all selected edges with two faces
	 * - find the faces: store them in edges (using datablock)
	 * - per edge: - test convex
	 *			   - test edge: flip?
						- if true: remedge,  addedge, all edges at the edge get new face pointers
	 */

	EM_selectmode_flush(em);	// makes sure in selectmode 'face' the edges of selected faces are selected too

	totedge = count_selected_edges(em->edges.first);
	if(totedge==0) return;

	/* temporary array for : edge -> face[1], face[2] */
	efaar= (EVPTuple *) MEM_callocN(totedge * sizeof(EVPTuple), "edgeflip");

	ok = collect_quadedges(efaar, em->edges.first, em->faces.first);

	eed= em->edges.first;
	while(eed) {
		nexted= eed->next;

		if(eed->f2==2) {  /* points to 2 faces */

			efaa= (EVPtr *) eed->tmp.p;

			/* don't do it if flagged */

			ok= 1;
			efa= efaa[0];
			if(efa->e1->f1 || efa->e2->f1 || efa->e3->f1) ok= 0;
			efa= efaa[1];
			if(efa->e1->f1 || efa->e2->f1 || efa->e3->f1) ok= 0;

			if(ok) {
				/* test convex */
				givequadverts(efaa[0], efaa[1], &v1, &v2, &v3, &v4, vindex);

/*
		4-----3		4-----3
		|\	|		|	/|
		| \ 1 |		| 1 / |
		|  \  |  ->	|  /  |
		| 0 \ |		| / 0 |
		|	\|		|/	|
		1-----2		1-----2
*/
				/* make new faces */
				if (v1 && v2 && v3) {
					if( convex(v1->co, v2->co, v3->co, v4->co) ) {
						if(exist_face(em, v1, v2, v3, v4)==0) {
							/* outch this may break seams */
							w= EM_face_from_faces(em, efaa[0], efaa[1], vindex[0],
								vindex[1], 4+vindex[2], -1);

							EM_select_face(w, 1);

							/* outch this may break seams */
							w= EM_face_from_faces(em, efaa[0], efaa[1], vindex[0],
								4+vindex[2], 4+vindex[3], -1);

							EM_select_face(w, 1);
						}
						/* tag as to-be-removed */
						FACE_MARKCLEAR(efaa[1]);
						FACE_MARKCLEAR(efaa[0]);
						eed->f1 = 1;

					} /* endif test convex */
				}
			}
		}
		eed= nexted;
	}

	/* clear tagged edges and faces: */
	free_tagged_edges_faces(em, em->edges.first, em->faces.first);

	MEM_freeN(efaar);
}


#define AXIS_X		1
#define AXIS_Y		2

static const EnumPropertyItem axis_items[]= {
	{AXIS_X, "X", 0, "X", ""},
	{AXIS_Y, "Y", 0, "Y", ""},
	{0, NULL, 0, NULL, NULL},
};

/******************* BEVEL CODE STARTS HERE ********************/

  /* XXX old bevel not ported yet */

void bevel_menu(EditMesh *em)
{
	BME_Mesh *bm;
	BME_TransData_Head *td;
//	TransInfo *t;
	int options, res, gbm_free = 0;

//	t = BIF_GetTransInfo();
	if (!G.editBMesh) {
		G.editBMesh = MEM_callocN(sizeof(*(G.editBMesh)),"bevel_menu() G.editBMesh");
		gbm_free = 1;
	}

	G.editBMesh->options = BME_BEVEL_RUNNING | BME_BEVEL_SELECT;
	G.editBMesh->res = 1;

	while(G.editBMesh->options & BME_BEVEL_RUNNING) {
		options = G.editBMesh->options;
		res = G.editBMesh->res;
		bm = BME_editmesh_to_bmesh(em);
//		BIF_undo_push("Pre-Bevel");
		free_editMesh(em);
		BME_bevel(bm,0.1f,res,options,0,0,&td);
		BME_bmesh_to_editmesh(bm, td, em);
		EM_selectmode_flush(em);
		G.editBMesh->bm = bm;
		G.editBMesh->td = td;
//		initTransform(TFM_BEVEL,CTX_BMESH);
//		Transform();
		BME_free_transdata(td);
		BME_free_mesh(bm);
//		if (t->state != TRANS_CONFIRM) {
//			BIF_undo();
//		}
		if (options == G.editBMesh->options) {
			G.editBMesh->options &= ~BME_BEVEL_RUNNING;
		}
	}

	if (gbm_free) {
		MEM_freeN(G.editBMesh);
		G.editBMesh = NULL;
	}
}


/* *********** END BEVEL *********/

/* this utility function checks to see if 2 edit edges share a face,
returns 1 if they do
returns 0 if they do not, or if the function is passed the same edge 2 times
*/
short sharesFace(EditMesh *em, EditEdge* e1, EditEdge* e2)
{
	EditFace *search=NULL;

	search = em->faces.first;
	if (e1 == e2){
		return 0 ;
	}
	while(search){
		if(
		   ((search->e1 == e1 || search->e2 == e1) || (search->e3 == e1 || search->e4 == e1)) &&
		   ((search->e1 == e2 || search->e2 == e2) || (search->e3 == e2 || search->e4 == e2))
		   ) {
			return 1;
		}
		search = search->next;
	}
	return 0;
}

/* -------------------- More tools ------------------ */
#if 0
void mesh_set_face_flags(EditMesh *em, short mode)
{
	EditFace *efa;
	MTFace *tface;
	short	m_tex=0, m_shared=0,
			m_light=0, m_invis=0, m_collision=0,
			m_twoside=0, m_obcolor=0, m_halo=0,
			m_billboard=0, m_shadow=0, m_text=0,
			m_sort=0;
	short flag = 0, change = 0;

// XXX	if (!EM_texFaceCheck()) {
//		error("not a mesh with uv/image layers");
//		return;
//	}

	add_numbut(0, TOG|SHO, "Texture", 0, 0, &m_tex, NULL);
	add_numbut(2, TOG|SHO, "Light", 0, 0, &m_light, NULL);
	add_numbut(3, TOG|SHO, "Invisible", 0, 0, &m_invis, NULL);
	add_numbut(4, TOG|SHO, "Collision", 0, 0, &m_collision, NULL);
	add_numbut(5, TOG|SHO, "Shared", 0, 0, &m_shared, NULL);
	add_numbut(6, TOG|SHO, "Twoside", 0, 0, &m_twoside, NULL);
	add_numbut(7, TOG|SHO, "ObColor", 0, 0, &m_obcolor, NULL);
	add_numbut(8, TOG|SHO, "Halo", 0, 0, &m_halo, NULL);
	add_numbut(9, TOG|SHO, "Billboard", 0, 0, &m_billboard, NULL);
	add_numbut(10, TOG|SHO, "Shadow", 0, 0, &m_shadow, NULL);
	add_numbut(11, TOG|SHO, "Text", 0, 0, &m_text, NULL);
	add_numbut(12, TOG|SHO, "Sort", 0, 0, &m_sort, NULL);

	if (!do_clever_numbuts((mode ? "Set Flags" : "Clear Flags"), 13, REDRAW))
 		return;

	/* these 2 cant both be on */
	if (mode) /* are we seeting*/
		if (m_halo)
			m_billboard = 0;

	if (m_tex)			flag |= TF_TEX;
	if (m_shared)		flag |= TF_SHAREDCOL;
	if (m_light)		flag |= TF_LIGHT;
	if (m_invis)		flag |= TF_INVISIBLE;
	if (m_collision)	flag |= TF_DYNAMIC;
	if (m_twoside)		flag |= TF_TWOSIDE;
	if (m_obcolor)		flag |= TF_OBCOL;
	if (m_halo)			flag |= TF_BILLBOARD;
	if (m_billboard)	flag |= TF_BILLBOARD2;
	if (m_shadow)		flag |= TF_SHADOW;
	if (m_text)			flag |= TF_BMFONT;
	if (m_sort)			flag |= TF_ALPHASORT;

	if (flag==0)
		return;

	efa= em->faces.first;
	while(efa) {
		if(efa->f & SELECT) {
			tface= CustomData_em_get(&em->fdata, efa->data, CD_MTFACE);
			if (mode)	tface->mode |= flag;
			else		tface->mode &= ~flag;
			change = 1;
		}
		efa= efa->next;
	}

}
#endif

/************************ Shape Operators *************************/

#if 0
void shape_propagate(Scene *scene, Object *obedit, EditMesh *em, wmOperator *op)
{
	EditVert *ev = NULL;
	Mesh* me = (Mesh*)obedit->data;
	Key*  ky = NULL;
	KeyBlock* kb = NULL;
	Base* base=NULL;


	if(me->key){
		ky = me->key;
	} else {
		BKE_report(op->reports, RPT_ERROR, "Object Has No Key");
		return;
	}

	if(ky->block.first){
		for(ev = em->verts.first; ev ; ev = ev->next){
			if(ev->f & SELECT){
				for(kb=ky->block.first;kb;kb = kb->next){
					float *data;
					data = kb->data;
					VECCOPY(data+(ev->keyindex*3),ev->co);
				}
			}
		}
	} else {
		BKE_report(op->reports, RPT_ERROR, "Object Has No Blendshapes");
		return;
	}

	//TAG Mesh Objects that share this data
	for(base = scene->base.first; base; base = base->next){
		if(base->object && base->object->data == me){
			base->object->recalc = OB_RECALC_DATA;
		}
	}

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	return;
}
#endif

static int blend_from_shape_exec(bContext *C, wmOperator *op)
{
#if 0 //BMESH_TODO
	EditVert *ev = NULL;
	short mval[2], curval[2], event = 0, finished = 0, canceled = 0, fullcopy=0 ;
	float perc = 0;
	char str[64];
	float *data, *odata;
	Object *obedit= CTX_data_edit_object(C);
	Mesh *me= obedit->data;
	Key *key= me->key;
	EditMesh *em= BKE_mesh_get_editmesh(me);
	EditVert *eve;
	KeyBlock *kb;
	float *data, co[3];
	float blend= RNA_float_get(op->ptr, "blend");
	int shape= RNA_enum_get(op->ptr, "shape");
	int add= RNA_int_get(op->ptr, "add");
	int blended= 0;

	if(key && (kb= BLI_findlink(&key->block, shape))) {
		data= kb->data;

		for(eve=em->verts.first; eve; eve=eve->next){
			if(eve->f & SELECT) {
				if(eve->keyindex >= 0 && eve->keyindex < kb->totelem) {
					VECCOPY(co, data + eve->keyindex*3);

					if(add) {
						VecMulf(co, blend);
						VecAddf(eve->co, eve->co, co);
					}
					else
						VecLerpf(eve->co, eve->co, co, blend);

					blended= 1;
				}
			}
		}
	}
	if(!canceled);
	else
		for(ev = em->verts.first; ev ; ev = ev->next){
			if(ev->f & SELECT){
				VECCOPY(ev->co, odata+(ev->keyindex*3));
			}
		}
	return;
#endif
	return OPERATOR_CANCELLED;
}

static EnumPropertyItem *shape_itemf(bContext *C, PointerRNA *ptr, int *free)
{	
	Object *obedit= CTX_data_edit_object(C);
	Mesh *me= (obedit) ? obedit->data : NULL;
	Key *key;
	KeyBlock *kb, *actkb;
	EnumPropertyItem tmp= {0, "", 0, "", ""}, *item= NULL;
	int totitem= 0, a;

	if(obedit && obedit->type == OB_MESH) {
		key= me->key;
		actkb= ob_get_keyblock(obedit);

		if(key && actkb) {
			for(kb=key->block.first, a=0; kb; kb=kb->next, a++) {
				if(kb != actkb) {
					tmp.value= a;
					tmp.identifier= kb->name;
					tmp.name= kb->name;
					RNA_enum_item_add(&item, &totitem, &tmp);
				}
			}
		}
	}

	RNA_enum_item_end(&item, &totitem);
	*free= 1;

	return item;
}

void MESH_OT_blend_from_shape(wmOperatorType *ot)
{
	PropertyRNA *prop;
	static EnumPropertyItem shape_items[]= {{0, NULL, 0, NULL, NULL}};

	/* identifiers */
	ot->name= "Blend From Shape";
	ot->description= "Blend in shape from a shape key.";
	ot->idname= "MESH_OT_blend_from_shape";

	/* api callbacks */
	ot->exec= blend_from_shape_exec;
	ot->invoke= WM_operator_props_popup;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	prop= RNA_def_enum(ot->srna, "shape", shape_items, 0, "Shape", "Shape key to use for blending.");
	RNA_def_enum_funcs(prop, shape_itemf);
	RNA_def_float(ot->srna, "blend", 1.0f, -FLT_MAX, FLT_MAX, "Blend", "Blending factor.", -2.0f, 2.0f);
	RNA_def_boolean(ot->srna, "add", 1, "Add", "Add rather then blend between shapes.");
}

/************************ Merge Operator *************************/

/* Collection Routines|Currently used by the improved merge code*/
/* buildEdge_collection() creates a list of lists*/
/* these lists are filled with edges that are topologically connected.*/
/* This whole tool needs to be redone, its rather poorly implemented...*/

typedef struct Collection{
	struct Collection *next, *prev;
	int index;
	ListBase collectionbase;
} Collection;

typedef struct CollectedEdge{
	struct CollectedEdge *next, *prev;
	EditEdge *eed;
} CollectedEdge;

#define MERGELIMIT 0.000001

static void build_edgecollection(EditMesh *em, ListBase *allcollections)
{
	EditEdge *eed;
	Collection *edgecollection, *newcollection;
	CollectedEdge *newedge;

	int currtag = 1;
	short ebalanced = 0;
	short collectionfound = 0;

	for (eed=em->edges.first; eed; eed = eed->next){
		eed->tmp.l = 0;
		eed->v1->tmp.l = 0;
		eed->v2->tmp.l = 0;
	}

	/*1st pass*/
	for(eed=em->edges.first; eed; eed=eed->next){
			if(eed->f&SELECT){
				eed->v1->tmp.l = currtag;
				eed->v2->tmp.l = currtag;
				currtag +=1;
			}
	}

	/*2nd pass - Brute force. Loop through selected faces until there are no 'unbalanced' edges left (those with both vertices 'tmp.l' tag matching */
	while(ebalanced == 0){
		ebalanced = 1;
		for(eed=em->edges.first; eed; eed = eed->next){
			if(eed->f&SELECT){
				if(eed->v1->tmp.l != eed->v2->tmp.l) /*unbalanced*/{
					if(eed->v1->tmp.l > eed->v2->tmp.l && eed->v2->tmp.l !=0) eed->v1->tmp.l = eed->v2->tmp.l;
					else if(eed->v1 != 0) eed->v2->tmp.l = eed->v1->tmp.l;
					ebalanced = 0;
				}
			}
		}
	}

	/*3rd pass, set all the edge flags (unnessecary?)*/
	for(eed=em->edges.first; eed; eed = eed->next){
		if(eed->f&SELECT) eed->tmp.l = eed->v1->tmp.l;
	}

	for(eed=em->edges.first; eed; eed=eed->next){
		if(eed->f&SELECT){
			if(allcollections->first){
				for(edgecollection = allcollections->first; edgecollection; edgecollection=edgecollection->next){
					if(edgecollection->index == eed->tmp.l){
						newedge = MEM_mallocN(sizeof(CollectedEdge), "collected edge");
						newedge->eed = eed;
						BLI_addtail(&(edgecollection->collectionbase), newedge);
						collectionfound = 1;
						break;
					}
					else collectionfound = 0;
				}
			}
			if(allcollections->first == NULL || collectionfound == 0){
				newcollection = MEM_mallocN(sizeof(Collection), "element collection");
				newcollection->index = eed->tmp.l;
				newcollection->collectionbase.first = 0;
				newcollection->collectionbase.last = 0;

				newedge = MEM_mallocN(sizeof(CollectedEdge), "collected edge");
				newedge->eed = eed;

				BLI_addtail(&(newcollection->collectionbase), newedge);
				BLI_addtail(allcollections, newcollection);
			}
		}

	}
}

static void freecollections(ListBase *allcollections)
{
	struct Collection *curcollection;

	for(curcollection = allcollections->first; curcollection; curcollection = curcollection->next)
		BLI_freelistN(&(curcollection->collectionbase));
	BLI_freelistN(allcollections);
}

/*Begin UV Edge Collapse Code
	Like Edge subdivide, Edge Collapse should handle UV's intelligently, but since UV's are a per-face attribute, normal edge collapse will fail
	in areas such as the boundries of 'UV islands'. So for each edge collection we need to build a set of 'welded' UV vertices and edges for it.
	The welded UV edges can then be sorted and collapsed.
*/
typedef struct wUV{
	struct wUV *next, *prev;
	ListBase nodes;
	float u, v; /*cached copy of UV coordinates pointed to by nodes*/
	EditVert *eve;
	int f;
} wUV;

typedef struct wUVNode{
	struct wUVNode *next, *prev;
	float *u; /*pointer to original tface data*/
	float *v; /*pointer to original tface data*/
} wUVNode;

typedef struct wUVEdge{
	struct wUVEdge *next, *prev;
	float v1uv[2], v2uv[2]; /*nasty.*/
	struct wUV *v1, *v2; /*oriented same as editedge*/
	EditEdge *eed;
	int f;
} wUVEdge;

typedef struct wUVEdgeCollect{ /*used for grouping*/
	struct wUVEdgeCollect *next, *prev;
	wUVEdge *uved;
	int id;
} wUVEdgeCollect;

static void append_weldedUV(EditMesh *em, EditFace *efa, EditVert *eve, int tfindex, ListBase *uvverts)
{
	wUV *curwvert, *newwvert;
	wUVNode *newnode;
	int found;
	MTFace *tf = CustomData_em_get(&em->fdata, efa->data, CD_MTFACE);

	found = 0;

	for(curwvert=uvverts->first; curwvert; curwvert=curwvert->next){
		if(curwvert->eve == eve && curwvert->u == tf->uv[tfindex][0] && curwvert->v == tf->uv[tfindex][1]){
			newnode = MEM_callocN(sizeof(wUVNode), "Welded UV Vert Node");
			newnode->u = &(tf->uv[tfindex][0]);
			newnode->v = &(tf->uv[tfindex][1]);
			BLI_addtail(&(curwvert->nodes), newnode);
			found = 1;
			break;
		}
	}

	if(!found){
		newnode = MEM_callocN(sizeof(wUVNode), "Welded UV Vert Node");
		newnode->u = &(tf->uv[tfindex][0]);
		newnode->v = &(tf->uv[tfindex][1]);

		newwvert = MEM_callocN(sizeof(wUV), "Welded UV Vert");
		newwvert->u = *(newnode->u);
		newwvert->v = *(newnode->v);
		newwvert->eve = eve;

		BLI_addtail(&(newwvert->nodes), newnode);
		BLI_addtail(uvverts, newwvert);

	}
}

static void build_weldedUVs(EditMesh *em, ListBase *uvverts)
{
	EditFace *efa;
	for(efa=em->faces.first; efa; efa=efa->next){
		if(efa->v1->f1) append_weldedUV(em, efa, efa->v1, 0, uvverts);
		if(efa->v2->f1) append_weldedUV(em, efa, efa->v2, 1, uvverts);
		if(efa->v3->f1) append_weldedUV(em, efa, efa->v3, 2, uvverts);
		if(efa->v4 && efa->v4->f1) append_weldedUV(em, efa, efa->v4, 3, uvverts);
	}
}

static void append_weldedUVEdge(EditMesh *em, EditFace *efa, EditEdge *eed, ListBase *uvedges)
{
	wUVEdge *curwedge, *newwedge;
	int v1tfindex, v2tfindex, found;
	MTFace *tf = CustomData_em_get(&em->fdata, efa->data, CD_MTFACE);

	found = 0;

	if(eed->v1 == efa->v1) v1tfindex = 0;
	else if(eed->v1 == efa->v2) v1tfindex = 1;
	else if(eed->v1 == efa->v3) v1tfindex = 2;
	else /* if(eed->v1 == efa->v4) */ v1tfindex = 3;

	if(eed->v2 == efa->v1) v2tfindex = 0;
	else if(eed->v2 == efa->v2) v2tfindex = 1;
	else if(eed->v2 == efa->v3) v2tfindex = 2;
	else /* if(eed->v2 == efa->v4) */ v2tfindex = 3;

	for(curwedge=uvedges->first; curwedge; curwedge=curwedge->next){
			if(curwedge->eed == eed && curwedge->v1uv[0] == tf->uv[v1tfindex][0] && curwedge->v1uv[1] == tf->uv[v1tfindex][1] && curwedge->v2uv[0] == tf->uv[v2tfindex][0] && curwedge->v2uv[1] == tf->uv[v2tfindex][1]){
				found = 1;
				break; //do nothing, we don't need another welded uv edge
			}
	}

	if(!found){
		newwedge = MEM_callocN(sizeof(wUVEdge), "Welded UV Edge");
		newwedge->v1uv[0] = tf->uv[v1tfindex][0];
		newwedge->v1uv[1] = tf->uv[v1tfindex][1];
		newwedge->v2uv[0] = tf->uv[v2tfindex][0];
		newwedge->v2uv[1] = tf->uv[v2tfindex][1];
		newwedge->eed = eed;

		BLI_addtail(uvedges, newwedge);
	}
}

static void build_weldedUVEdges(EditMesh *em, ListBase *uvedges, ListBase *uvverts)
{
	wUV *curwvert;
	wUVEdge *curwedge;
	EditFace *efa;

	for(efa=em->faces.first; efa; efa=efa->next){
		if(efa->e1->f1) append_weldedUVEdge(em, efa, efa->e1, uvedges);
		if(efa->e2->f1) append_weldedUVEdge(em, efa, efa->e2, uvedges);
		if(efa->e3->f1) append_weldedUVEdge(em, efa, efa->e3, uvedges);
		if(efa->e4 && efa->e4->f1) append_weldedUVEdge(em, efa, efa->e4, uvedges);
	}


	//link vertices: for each uvedge, search uvverts to populate v1 and v2 pointers
	for(curwedge=uvedges->first; curwedge; curwedge=curwedge->next){
		for(curwvert=uvverts->first; curwvert; curwvert=curwvert->next){
			if(curwedge->eed->v1 == curwvert->eve && curwedge->v1uv[0] == curwvert->u && curwedge->v1uv[1] == curwvert->v){
				curwedge->v1 = curwvert;
				break;
			}
		}
		for(curwvert=uvverts->first; curwvert; curwvert=curwvert->next){
			if(curwedge->eed->v2 == curwvert->eve && curwedge->v2uv[0] == curwvert->u && curwedge->v2uv[1] == curwvert->v){
				curwedge->v2 = curwvert;
				break;
			}
		}
	}
}

static void free_weldedUVs(ListBase *uvverts)
{
	wUV *curwvert;
	for(curwvert = uvverts->first; curwvert; curwvert=curwvert->next) BLI_freelistN(&(curwvert->nodes));
	BLI_freelistN(uvverts);
}

static void collapse_edgeuvs(EditMesh *em)
{
	ListBase uvedges, uvverts, allcollections;
	wUVEdge *curwedge;
	wUVNode *curwnode;
	wUVEdgeCollect *collectedwuve, *newcollectedwuve;
	Collection *wuvecollection, *newcollection;
	int curtag, balanced, collectionfound= 0, vcount;
	float avg[2];

	if (!EM_texFaceCheck(em))
		return;

	uvverts.first = uvverts.last = uvedges.first = uvedges.last = allcollections.first = allcollections.last = NULL;

	build_weldedUVs(em, &uvverts);
	build_weldedUVEdges(em, &uvedges, &uvverts);

	curtag = 0;

	for(curwedge=uvedges.first; curwedge; curwedge=curwedge->next){
		curwedge->v1->f = curtag;
		curwedge->v2->f = curtag;
		curtag +=1;
	}

	balanced = 0;
	while(!balanced){
		balanced = 1;
		for(curwedge=uvedges.first; curwedge; curwedge=curwedge->next){
			if(curwedge->v1->f != curwedge->v2->f){
				if(curwedge->v1->f > curwedge->v2->f) curwedge->v1->f = curwedge->v2->f;
				else curwedge->v2->f = curwedge->v1->f;
				balanced = 0;
			}
		}
	}

	for(curwedge=uvedges.first; curwedge; curwedge=curwedge->next) curwedge->f = curwedge->v1->f;


	for(curwedge=uvedges.first; curwedge; curwedge=curwedge->next){
		if(allcollections.first){
			for(wuvecollection = allcollections.first; wuvecollection; wuvecollection=wuvecollection->next){
				if(wuvecollection->index == curwedge->f){
					newcollectedwuve = MEM_callocN(sizeof(wUVEdgeCollect), "Collected Welded UV Edge");
					newcollectedwuve->uved = curwedge;
					BLI_addtail(&(wuvecollection->collectionbase), newcollectedwuve);
					collectionfound = 1;
					break;
				}

				else collectionfound = 0;
			}
		}
		if(allcollections.first == NULL || collectionfound == 0){
			newcollection = MEM_callocN(sizeof(Collection), "element collection");
			newcollection->index = curwedge->f;
			newcollection->collectionbase.first = 0;
			newcollection->collectionbase.last = 0;

			newcollectedwuve = MEM_callocN(sizeof(wUVEdgeCollect), "Collected Welded UV Edge");
			newcollectedwuve->uved = curwedge;

			BLI_addtail(&(newcollection->collectionbase), newcollectedwuve);
			BLI_addtail(&allcollections, newcollection);
		}
	}

	for(wuvecollection=allcollections.first; wuvecollection; wuvecollection=wuvecollection->next){

		vcount = avg[0] = avg[1] = 0;

		for(collectedwuve= wuvecollection->collectionbase.first; collectedwuve; collectedwuve = collectedwuve->next){
			avg[0] += collectedwuve->uved->v1uv[0];
			avg[1] += collectedwuve->uved->v1uv[1];

			avg[0] += collectedwuve->uved->v2uv[0];
			avg[1] += collectedwuve->uved->v2uv[1];

			vcount +=2;

		}

		avg[0] /= vcount; avg[1] /= vcount;

		for(collectedwuve= wuvecollection->collectionbase.first; collectedwuve; collectedwuve = collectedwuve->next){
			for(curwnode=collectedwuve->uved->v1->nodes.first; curwnode; curwnode=curwnode->next){
				*(curwnode->u) = avg[0];
				*(curwnode->v) = avg[1];
			}
			for(curwnode=collectedwuve->uved->v2->nodes.first; curwnode; curwnode=curwnode->next){
				*(curwnode->u) = avg[0];
				*(curwnode->v) = avg[1];
			}
		}
	}

	free_weldedUVs(&uvverts);
	BLI_freelistN(&uvedges);
	freecollections(&allcollections);
}

/*End UV Edge collapse code*/

static void collapseuvs(EditMesh *em, EditVert *mergevert)
{
	EditFace *efa;
	MTFace *tf;
	int uvcount;
	float uvav[2];

	if (!EM_texFaceCheck(em))
		return;

	uvcount = 0;
	uvav[0] = 0;
	uvav[1] = 0;

	for(efa = em->faces.first; efa; efa=efa->next){
		tf = CustomData_em_get(&em->fdata, efa->data, CD_MTFACE);

		if(efa->v1->f1 && ELEM(mergevert, NULL, efa->v1)) {
			uvav[0] += tf->uv[0][0];
			uvav[1] += tf->uv[0][1];
			uvcount += 1;
		}
		if(efa->v2->f1 && ELEM(mergevert, NULL, efa->v2)){
			uvav[0] += tf->uv[1][0];
			uvav[1] += tf->uv[1][1];
			uvcount += 1;
		}
		if(efa->v3->f1 && ELEM(mergevert, NULL, efa->v3)){
			uvav[0] += tf->uv[2][0];
			uvav[1] += tf->uv[2][1];
			uvcount += 1;
		}
		if(efa->v4 && efa->v4->f1 && ELEM(mergevert, NULL, efa->v4)){
			uvav[0] += tf->uv[3][0];
			uvav[1] += tf->uv[3][1];
			uvcount += 1;
		}
	}

	if(uvcount > 0) {
		uvav[0] /= uvcount;
		uvav[1] /= uvcount;

		for(efa = em->faces.first; efa; efa=efa->next){
			tf = CustomData_em_get(&em->fdata, efa->data, CD_MTFACE);

			if(efa->v1->f1){
				tf->uv[0][0] = uvav[0];
				tf->uv[0][1] = uvav[1];
			}
			if(efa->v2->f1){
				tf->uv[1][0] = uvav[0];
				tf->uv[1][1] = uvav[1];
			}
			if(efa->v3->f1){
				tf->uv[2][0] = uvav[0];
				tf->uv[2][1] = uvav[1];
			}
			if(efa->v4 && efa->v4->f1){
				tf->uv[3][0] = uvav[0];
				tf->uv[3][1] = uvav[1];
			}
		}
	}
}

int collapseEdges(EditMesh *em)
{
	EditVert *eve;
	EditEdge *eed;

	ListBase allcollections;
	CollectedEdge *curredge;
	Collection *edgecollection;

	int totedges, groupcount, mergecount,vcount;
	float avgcount[3];

	allcollections.first = 0;
	allcollections.last = 0;

	mergecount = 0;

	build_edgecollection(em, &allcollections);
	groupcount = BLI_countlist(&allcollections);


	for(edgecollection = allcollections.first; edgecollection; edgecollection = edgecollection->next){
		totedges = BLI_countlist(&(edgecollection->collectionbase));
		mergecount += totedges;
		avgcount[0] = 0; avgcount[1] = 0; avgcount[2] = 0;

		vcount = 0;

		for(curredge = edgecollection->collectionbase.first; curredge; curredge = curredge->next){
			avgcount[0] += ((EditEdge*)curredge->eed)->v1->co[0];
			avgcount[1] += ((EditEdge*)curredge->eed)->v1->co[1];
			avgcount[2] += ((EditEdge*)curredge->eed)->v1->co[2];

			avgcount[0] += ((EditEdge*)curredge->eed)->v2->co[0];
			avgcount[1] += ((EditEdge*)curredge->eed)->v2->co[1];
			avgcount[2] += ((EditEdge*)curredge->eed)->v2->co[2];

			vcount +=2;
		}

		avgcount[0] /= vcount; avgcount[1] /=vcount; avgcount[2] /= vcount;

		for(curredge = edgecollection->collectionbase.first; curredge; curredge = curredge->next){
			VECCOPY(((EditEdge*)curredge->eed)->v1->co,avgcount);
			VECCOPY(((EditEdge*)curredge->eed)->v2->co,avgcount);
		}

		if (EM_texFaceCheck(em)) {
			/*uv collapse*/
			for(eve=em->verts.first; eve; eve=eve->next) eve->f1 = 0;
			for(eed=em->edges.first; eed; eed=eed->next) eed->f1 = 0;
			for(curredge = edgecollection->collectionbase.first; curredge; curredge = curredge->next){
				curredge->eed->v1->f1 = 1;
				curredge->eed->v2->f1 = 1;
				curredge->eed->f1 = 1;
			}
			collapse_edgeuvs(em);
		}

	}
	freecollections(&allcollections);
	removedoublesflag(em, 1, 0, MERGELIMIT);

	return mergecount;
}



/********************** Region/Loop Operators *************************/

static int region_to_loop(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);
	EditEdge *eed;
	EditFace *efa;
	int selected= 0;

	for(eed=em->edges.first; eed; eed=eed->next) eed->f1 = 0;

	for(efa=em->faces.first; efa; efa=efa->next){
		if(efa->f&SELECT){
			efa->e1->f1++;
			efa->e2->f1++;
			efa->e3->f1++;
			if(efa->e4)
				efa->e4->f1++;

			selected= 1;
		}
	}

	if(!selected)
		return OPERATOR_CANCELLED;

	EM_clear_flag_all(em, SELECT);

	for(eed=em->edges.first; eed; eed=eed->next){
		if(eed->f1 == 1) EM_select_edge(eed, 1);
	}

	em->selectmode = SCE_SELECT_EDGE;
	EM_selectmode_set(em);

	BKE_mesh_end_editmesh(obedit->data, em);

	WM_event_add_notifier(C, NC_GEOM|ND_SELECT, obedit->data);

	return OPERATOR_FINISHED;
}

void MESH_OT_region_to_loop(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Region to Loop";
	ot->idname= "MESH_OT_region_to_loop";

	/* api callbacks */
	ot->exec= region_to_loop;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

static int validate_loop(EditMesh *em, Collection *edgecollection)
{
	EditEdge *eed;
	EditFace *efa;
	CollectedEdge *curredge;

	/*1st test*/
	for(curredge = (CollectedEdge*)edgecollection->collectionbase.first; curredge; curredge=curredge->next){
		curredge->eed->v1->f1 = 0;
		curredge->eed->v2->f1 = 0;
	}
	for(curredge = (CollectedEdge*)edgecollection->collectionbase.first; curredge; curredge=curredge->next){
		curredge->eed->v1->f1++;
		curredge->eed->v2->f1++;
	}
	for(curredge = (CollectedEdge*)edgecollection->collectionbase.first; curredge; curredge=curredge->next){
		if(curredge->eed->v1->f1 > 2) return(0); else
		if(curredge->eed->v2->f1 > 2) return(0);
	}

	/*2nd test*/
	for(eed = em->edges.first; eed; eed=eed->next) eed->f1 = 0;
	for(efa=em->faces.first; efa; efa=efa->next){
		efa->e1->f1++;
		efa->e2->f1++;
		efa->e3->f1++;
		if(efa->e4) efa->e4->f1++;
	}
	for(curredge = (CollectedEdge*)edgecollection->collectionbase.first; curredge; curredge=curredge->next){
		if(curredge->eed->f1 > 2) return(0);
	}
	return(1);
}

static int loop_bisect(EditMesh *em, Collection *edgecollection)
{
	EditFace *efa, *sf1, *sf2;
	EditEdge *eed, *sed;
	CollectedEdge *curredge;
	int totsf1, totsf2, unbalanced,balancededges;

	for(eed=em->edges.first; eed; eed=eed->next) eed->f1 = eed->f2 = 0;
	for(efa=em->faces.first; efa; efa=efa->next) efa->f1 = 0;

	for(curredge = (CollectedEdge*)edgecollection->collectionbase.first; curredge; curredge=curredge->next) curredge->eed->f1 = 1;

	sf1 = sf2 = NULL;
	sed = ((CollectedEdge*)edgecollection->collectionbase.first)->eed;

	for(efa=em->faces.first; efa; efa=efa->next){
		if(sf2) break;
		else if(sf1){
			if(efa->e1 == sed || efa->e2 == sed || efa->e3 == sed || ( (efa->e4) ? efa->e4 == sed : 0) ) sf2 = efa;
		}
		else{
			if(efa->e1 == sed || efa->e2 == sed || efa->e3 == sed || ( (efa->e4) ? efa->e4 == sed : 0) ) sf1 = efa;
		}
	}

	if(sf1==NULL || sf2==NULL)
		return(-1);

	if(!(sf1->e1->f1)) sf1->e1->f2 = 1;
	if(!(sf1->e2->f1)) sf1->e2->f2 = 1;
	if(!(sf1->e3->f1)) sf1->e3->f2 = 1;
	if(sf1->e4 && !(sf1->e4->f1)) sf1->e4->f2 = 1;
	sf1->f1 = 1;
	totsf1 = 1;

	if(!(sf2->e1->f1)) sf2->e1->f2 = 2;
	if(!(sf2->e2->f1)) sf2->e2->f2 = 2;
	if(!(sf2->e3->f1)) sf2->e3->f2 = 2;
	if(sf2->e4 && !(sf2->e4->f1)) sf2->e4->f2 = 2;
	sf2->f1 = 2;
	totsf2 = 1;

	/*do sf1*/
	unbalanced = 1;
	while(unbalanced){
		unbalanced = 0;
		for(efa=em->faces.first; efa; efa=efa->next){
			balancededges = 0;
			if(efa->f1 == 0){
				if(efa->e1->f2 == 1 || efa->e2->f2 == 1 || efa->e3->f2 == 1 || ( (efa->e4) ? efa->e4->f2 == 1 : 0) ){
					balancededges += efa->e1->f2 = (efa->e1->f1) ? 0 : 1;
					balancededges += efa->e2->f2 = (efa->e2->f1) ? 0 : 1;
					balancededges += efa->e3->f2 = (efa->e3->f1) ? 0 : 1;
					if(efa->e4) balancededges += efa->e4->f2 = (efa->e4->f1) ? 0 : 1;
					if(balancededges){
						unbalanced = 1;
						efa->f1 = 1;
						totsf1++;
					}
				}
			}
		}
	}

	/*do sf2*/
	unbalanced = 1;
	while(unbalanced){
		unbalanced = 0;
		for(efa=em->faces.first; efa; efa=efa->next){
			balancededges = 0;
			if(efa->f1 == 0){
				if(efa->e1->f2 == 2 || efa->e2->f2 == 2 || efa->e3->f2 == 2 || ( (efa->e4) ? efa->e4->f2 == 2 : 0) ){
					balancededges += efa->e1->f2 = (efa->e1->f1) ? 0 : 2;
					balancededges += efa->e2->f2 = (efa->e2->f1) ? 0 : 2;
					balancededges += efa->e3->f2 = (efa->e3->f1) ? 0 : 2;
					if(efa->e4) balancededges += efa->e4->f2 = (efa->e4->f1) ? 0 : 2;
					if(balancededges){
						unbalanced = 1;
						efa->f1 = 2;
						totsf2++;
					}
				}
			}
		}
	}

	if(totsf1 < totsf2) return(1);
	else return(2);
}

static int loop_to_region(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);


	EditFace *efa;
	ListBase allcollections={NULL,NULL};
	Collection *edgecollection;
	int testflag;

	build_edgecollection(em, &allcollections);

	for(edgecollection = (Collection *)allcollections.first; edgecollection; edgecollection=edgecollection->next){
		if(validate_loop(em, edgecollection)){
			testflag = loop_bisect(em, edgecollection);
			for(efa=em->faces.first; efa; efa=efa->next){
				if(efa->f1 == testflag){
					if(efa->f&SELECT) EM_select_face(efa, 0);
					else EM_select_face(efa,1);
				}
			}
		}
	}

	for(efa=em->faces.first; efa; efa=efa->next){ /*fix this*/
		if(efa->f&SELECT) EM_select_face(efa,1);
	}

	freecollections(&allcollections);
	BKE_mesh_end_editmesh(obedit->data, em);

	WM_event_add_notifier(C, NC_GEOM|ND_SELECT, obedit->data);

	return OPERATOR_FINISHED;
}

void MESH_OT_loop_to_region(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Loop to Region";
	ot->idname= "MESH_OT_loop_to_region";

	/* api callbacks */
	ot->exec= loop_to_region;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/********************** Fill Operators *************************/

/* note; the EM_selectmode_set() calls here illustrate how badly constructed it all is... from before the
edge/face flags, with very mixed results.... */
static void beauty_fill(EditMesh *em)
{
	EditVert *v1, *v2, *v3, *v4;
	EditEdge *eed, *nexted;
	EditEdge dia1, dia2;
	EditFace *efa, *w;
	// void **efaar, **efaa;
	EVPTuple *efaar;
	EVPtr *efaa;
	float len1, len2, len3, len4, len5, len6, opp1, opp2, fac1, fac2;
	int totedge, ok, notbeauty=8, onedone, vindex[4];

	/* - all selected edges with two faces
		* - find the faces: store them in edges (using datablock)
		* - per edge: - test convex
		*			   - test edge: flip?
		*			   - if true: remedge,  addedge, all edges at the edge get new face pointers
		*/

	EM_selectmode_set(em);	// makes sure in selectmode 'face' the edges of selected faces are selected too

	totedge = count_selected_edges(em->edges.first);
	if(totedge==0) return;

	/* temp block with face pointers */
	efaar= (EVPTuple *) MEM_callocN(totedge * sizeof(EVPTuple), "beautyfill");

	while (notbeauty) {
		notbeauty--;

		ok = collect_quadedges(efaar, em->edges.first, em->faces.first);

		/* there we go */
		onedone= 0;

		eed= em->edges.first;
		while(eed) {
			nexted= eed->next;

			/* f2 is set in collect_quadedges() */
			if(eed->f2==2 && eed->h==0) {

				efaa = (EVPtr *) eed->tmp.p;

				/* none of the faces should be treated before, nor be part of fgon */
				ok= 1;
				efa= efaa[0];
				if(efa->e1->f1 || efa->e2->f1 || efa->e3->f1) ok= 0;
				if(efa->fgonf) ok= 0;
				efa= efaa[1];
				if(efa->e1->f1 || efa->e2->f1 || efa->e3->f1) ok= 0;
				if(efa->fgonf) ok= 0;

				if(ok) {
					/* test convex */
					givequadverts(efaa[0], efaa[1], &v1, &v2, &v3, &v4, vindex);
					if(v1 && v2 && v3 && v4) {
						if( convex(v1->co, v2->co, v3->co, v4->co) ) {

							/* test edges */
							if( (v1) > (v3) ) {
								dia1.v1= v3;
								dia1.v2= v1;
							}
							else {
								dia1.v1= v1;
								dia1.v2= v3;
							}

							if( (v2) > (v4) ) {
								dia2.v1= v4;
								dia2.v2= v2;
							}
							else {
								dia2.v1= v2;
								dia2.v2= v4;
							}

							/* testing rule:
							* the area divided by the total edge lengths
							*/

							len1= VecLenf(v1->co, v2->co);
							len2= VecLenf(v2->co, v3->co);
							len3= VecLenf(v3->co, v4->co);
							len4= VecLenf(v4->co, v1->co);
							len5= VecLenf(v1->co, v3->co);
							len6= VecLenf(v2->co, v4->co);

							opp1= AreaT3Dfl(v1->co, v2->co, v3->co);
							opp2= AreaT3Dfl(v1->co, v3->co, v4->co);

							fac1= opp1/(len1+len2+len5) + opp2/(len3+len4+len5);

							opp1= AreaT3Dfl(v2->co, v3->co, v4->co);
							opp2= AreaT3Dfl(v2->co, v4->co, v1->co);

							fac2= opp1/(len2+len3+len6) + opp2/(len4+len1+len6);

							ok= 0;
							if(fac1 > fac2) {
								if(dia2.v1==eed->v1 && dia2.v2==eed->v2) {
									eed->f1= 1;
									efa= efaa[0];
									efa->f1= 1;
									efa= efaa[1];
									efa->f1= 1;

									w= EM_face_from_faces(em, efaa[0], efaa[1],
														  vindex[0], vindex[1], 4+vindex[2], -1);
									w->f |= SELECT;


									w= EM_face_from_faces(em, efaa[0], efaa[1],
														  vindex[0], 4+vindex[2], 4+vindex[3], -1);
									w->f |= SELECT;

									onedone= 1;
								}
							}
							else if(fac1 < fac2) {
								if(dia1.v1==eed->v1 && dia1.v2==eed->v2) {
									eed->f1= 1;
									efa= efaa[0];
									efa->f1= 1;
									efa= efaa[1];
									efa->f1= 1;


									w= EM_face_from_faces(em, efaa[0], efaa[1],
														  vindex[1], 4+vindex[2], 4+vindex[3], -1);
									w->f |= SELECT;


									w= EM_face_from_faces(em, efaa[0], efaa[1],
														  vindex[0], 4+vindex[1], 4+vindex[3], -1);
									w->f |= SELECT;

									onedone= 1;
								}
							}
						}
					}
				}

			}
			eed= nexted;
		}

		free_tagged_edges_faces(em, em->edges.first, em->faces.first);

		if(onedone==0) break;

		EM_selectmode_set(em);	// new edges/faces were added
	}

	MEM_freeN(efaar);

	EM_select_flush(em);

}

/* Got this from scanfill.c. You will need to juggle around the
* callbacks for the scanfill.c code a bit for this to work. */
static void fill_mesh(EditMesh *em)
{
	EditVert *eve,*v1;
	EditEdge *eed,*e1,*nexted;
	EditFace *efa,*nextvl, *efan;
	short ok;

	if(em==NULL) return;
	waitcursor(1);

	/* copy all selected vertices */
	eve= em->verts.first;
	while(eve) {
		if(eve->f & SELECT) {
			v1= BLI_addfillvert(eve->co);
			eve->tmp.v= v1;
			v1->tmp.v= eve;
			v1->xs= 0;	// used for counting edges
		}
		eve= eve->next;
	}
	/* copy all selected edges */
	eed= em->edges.first;
	while(eed) {
		if( (eed->v1->f & SELECT) && (eed->v2->f & SELECT) ) {
			e1= BLI_addfilledge(eed->v1->tmp.v, eed->v2->tmp.v);
			e1->v1->xs++;
			e1->v2->xs++;
		}
		eed= eed->next;
	}
	/* from all selected faces: remove vertices and edges to prevent doubles */
	/* all edges add values, faces subtract,
		then remove edges with vertices ->xs<2 */
	efa= em->faces.first;
	ok= 0;
	while(efa) {
		nextvl= efa->next;
		if( faceselectedAND(efa, 1) ) {
			efa->v1->tmp.v->xs--;
			efa->v2->tmp.v->xs--;
			efa->v3->tmp.v->xs--;
			if(efa->v4) efa->v4->tmp.v->xs--;
			ok= 1;

		}
		efa= nextvl;
	}
	if(ok) {	/* there are faces selected */
		eed= filledgebase.first;
		while(eed) {
			nexted= eed->next;
			if(eed->v1->xs<2 || eed->v2->xs<2) {
				BLI_remlink(&filledgebase,eed);
			}
			eed= nexted;
		}
	}

	if(BLI_edgefill(0, em->mat_nr)) {
		efa= fillfacebase.first;
		while(efa) {
			/* normals default pointing up */
			efan= addfacelist(em, efa->v3->tmp.v, efa->v2->tmp.v,
							  efa->v1->tmp.v, 0, NULL, NULL);
			if(efan) EM_select_face(efan, 1);
			efa= efa->next;
		}
	}

	BLI_end_edgefill();
	beauty_fill(em);

	WM_cursor_wait(0);
	EM_select_flush(em);

}

static int fill_mesh_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	fill_mesh(em);

	BKE_mesh_end_editmesh(obedit->data, em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	return OPERATOR_FINISHED;

}

void MESH_OT_fill(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Fill";
	ot->idname= "MESH_OT_fill";

	/* api callbacks */
	ot->exec= fill_mesh_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

static int beauty_fill_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	beauty_fill(em);

	BKE_mesh_end_editmesh(obedit->data, em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	return OPERATOR_FINISHED;
}

void MESH_OT_beauty_fill(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Beauty Fill";
	ot->idname= "MESH_OT_beauty_fill";

	/* api callbacks */
	ot->exec= beauty_fill_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/********************** Quad/Tri Operators *************************/

static int quads_convert_to_tris_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	BMEditMesh *em= ((Mesh *)obedit->data)->edit_btmesh;

	//convert_to_triface(em,0);
	if (!EDBM_CallOpf(em, op, "triangulate faces=%hf", BM_SELECT))
		return OPERATOR_CANCELLED;

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	return OPERATOR_FINISHED;
}

void MESH_OT_quads_convert_to_tris(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Quads to Tris";
	ot->idname= "MESH_OT_quads_convert_to_tris";

	/* api callbacks */
	ot->exec= quads_convert_to_tris_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

static int tris_convert_to_quads_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	join_triangles(em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	BKE_mesh_end_editmesh(obedit->data, em);
	return OPERATOR_FINISHED;
}

void MESH_OT_tris_convert_to_quads(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Tris to Quads";
	ot->idname= "MESH_OT_tris_convert_to_quads";

	/* api callbacks */
	ot->exec= tris_convert_to_quads_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

static int edge_flip_exec(bContext *C, wmOperator *op)
{
	Object *obedit= CTX_data_edit_object(C);
	EditMesh *em= BKE_mesh_get_editmesh((Mesh *)obedit->data);

	edge_flip(em);

	DAG_id_flush_update(obedit->data, OB_RECALC_DATA);
	WM_event_add_notifier(C, NC_GEOM|ND_DATA, obedit->data);

	BKE_mesh_end_editmesh(obedit->data, em);
	return OPERATOR_FINISHED;
}

void MESH_OT_edge_flip(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Edge Flip";
	ot->idname= "MESH_OT_edge_flip";

	/* api callbacks */
	ot->exec= edge_flip_exec;
	ot->poll= ED_operator_editmesh;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}
