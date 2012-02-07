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
 * Contributor(s): Joseph Eagar.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_listBase.h"
#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_key_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_customdata.h"
#include "BKE_mesh.h"
#include "BKE_global.h"
#include "BKE_DerivedMesh.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_key.h"
#include "BKE_main.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_edgehash.h"
#include "BLI_editVert.h"
#include "BLI_scanfill.h"
#include "BLI_array.h"
#include "BLI_utildefines.h"

#include "ED_mesh.h"

#include "mesh_intern.h"
#include "bmesh.h"
#include "bmesh_private.h"

/*
 * MESH CONV.C
 *
 * This file contains functions
 * for converting a Mesh
 * into a Bmesh, and back again.
 *
 */

void mesh_to_bmesh_exec(BMesh *bm, BMOperator *op)
{
	Object *ob = BMO_Get_Pnt(op, "object");
	Mesh *me = BMO_Get_Pnt(op, "mesh");
	MVert *mvert;
	BLI_array_declare(verts);
	MEdge *medge;
	MLoop *ml;
	MPoly *mpoly;
	KeyBlock *actkey, *block;
	BMVert *v, **vt = NULL, **verts = NULL;
	BMEdge *e, **fedges = NULL, **et = NULL;
	BMFace *f;
	BMLoop *l;
	BLI_array_declare(fedges);
	float (*keyco)[3] = NULL;
	int *keyi;
	int set_key = BMO_Get_Int(op, "set_shapekey");
	int totuv, i, j, allocsize[4] = {512, 512, 2048, 512};

	if (!me || !me->totvert) {
		return; /* sanity check */
	}
	
	vt = MEM_mallocN(sizeof(void **) * me->totvert, "mesh to bmesh vtable");

	CustomData_copy(&me->vdata, &bm->vdata, CD_MASK_BMESH, CD_CALLOC, 0);
	CustomData_copy(&me->edata, &bm->edata, CD_MASK_BMESH, CD_CALLOC, 0);
	CustomData_copy(&me->ldata, &bm->ldata, CD_MASK_BMESH, CD_CALLOC, 0);
	CustomData_copy(&me->pdata, &bm->pdata, CD_MASK_BMESH, CD_CALLOC, 0);
	
	/* make sure uv layer names are consisten */
	totuv = CustomData_number_of_layers(&bm->pdata, CD_MTEXPOLY);
	for (i = 0; i < totuv; i++) {
		int li = CustomData_get_layer_index_n(&bm->pdata, CD_MTEXPOLY, i);
		CustomData_set_layer_name(&bm->ldata, CD_MLOOPUV, i, bm->pdata.layers[li].name);
	}
	
	if (!CustomData_has_layer(&bm->edata, CD_CREASE))
		CustomData_add_layer(&bm->edata, CD_CREASE, CD_ASSIGN, NULL, 0);

	if (!CustomData_has_layer(&bm->edata, CD_BWEIGHT))
		CustomData_add_layer(&bm->edata, CD_BWEIGHT, CD_ASSIGN, NULL, 0);

	if (!CustomData_has_layer(&bm->vdata, CD_BWEIGHT))
		CustomData_add_layer(&bm->vdata, CD_BWEIGHT, CD_ASSIGN, NULL, 0);


	if (me->key && ob->shapenr > me->key->totkey) {
		ob->shapenr = me->key->totkey - 1;
	}

	actkey = ob_get_keyblock(ob);
	if (actkey && actkey->totelem == me->totvert) {
		CustomData_add_layer(&bm->vdata, CD_SHAPE_KEYINDEX, CD_ASSIGN, NULL, 0);
		
		/* check if we need to generate unique ids for the shapekeys.
		 * this also exists in the file reading code, but is here for
		 * a sanity chec */
		if (!me->key->uidgen) {
			fprintf(stderr,
			        "%s had to generate shape key uid's in a situation we shouldn't need to! "
			        "(bmesh internal error)\n",
			        __func__);

			me->key->uidgen = 1;
			for (block = me->key->block.first; block; block = block->next) {
				block->uid = me->key->uidgen++;
			}
		}

		keyco = actkey->data;
		bm->shapenr = ob->shapenr;
		for (i = 0, block = me->key->block.first; block; block = block->next, i++) {
			CustomData_add_layer_named(&bm->vdata, CD_SHAPEKEY,
			                           CD_ASSIGN, NULL, 0, block->name);
			
			j = CustomData_get_layer_index_n(&bm->vdata, CD_SHAPEKEY, i);
			bm->vdata.layers[j].uid = block->uid;
		}
	}
	else if (actkey) {
		printf("shapekey<->mesh mismatch!\n");
	}
	
	CustomData_bmesh_init_pool(&bm->vdata, allocsize[0]);
	CustomData_bmesh_init_pool(&bm->edata, allocsize[1]);
	CustomData_bmesh_init_pool(&bm->ldata, allocsize[2]);
	CustomData_bmesh_init_pool(&bm->pdata, allocsize[3]);

	for (i = 0, mvert = me->mvert; i < me->totvert; i++, mvert++) {
		v = BM_Make_Vert(bm, keyco && set_key ? keyco[i] : mvert->co, NULL);
		BM_SetIndex(v, i); /* set_ok */
		vt[i] = v;

		/* transfer flag */
		v->head.hflag = BM_Vert_Flag_From_MEFlag(mvert->flag);

		/* this is necassary for selection counts to work properl */
		if (BM_TestHFlag(v, BM_SELECT)) BM_Select_Vert(bm, v, TRUE);

		normal_short_to_float_v3(v->no, mvert->no);

		BM_SetCDf(&bm->vdata, v, CD_BWEIGHT, (float)mvert->bweight / 255.0f);

		/* Copy Custom Dat */
		CustomData_to_bmesh_block(&me->vdata, &bm->vdata, i, &v->head.data);

		/* set shapekey dat */
		if (me->key) {
			/* set shape key original inde */
			keyi = CustomData_bmesh_get(&bm->vdata, v->head.data, CD_SHAPE_KEYINDEX);
			if (keyi) {
				*keyi = i;
			}
			
			for (block = me->key->block.first, j = 0; block; block = block->next, j++) {
				float *co = CustomData_bmesh_get_n(&bm->vdata, v->head.data, CD_SHAPEKEY, j);

				if (co) {
					copy_v3_v3(co, ((float *)block->data) + 3 * i);
				}
			}
		}
	}

	bm->elem_index_dirty &= ~BM_VERT; /* added in order, clear dirty flag */

	if (!me->totedge) {
		MEM_freeN(vt);
		return;
	}

	et = MEM_mallocN(sizeof(void **) * me->totedge, "mesh to bmesh etable");

	medge = me->medge;
	for (i = 0; i < me->totedge; i++, medge++) {
		e = BM_Make_Edge(bm, vt[medge->v1], vt[medge->v2], NULL, FALSE);
		BM_SetIndex(e, i); /* set_ok */
		et[i] = e;

		/* transfer flags */
		e->head.hflag = BM_Edge_Flag_From_MEFlag(medge->flag);

		/* this is necassary for selection counts to work properly */
		if (BM_TestHFlag(e, BM_SELECT)) BM_Select(bm, e, TRUE);
		
		/* Copy Custom Dat */
		CustomData_to_bmesh_block(&me->edata, &bm->edata, i, &e->head.data);
		
		BM_SetCDf(&bm->edata, e, CD_CREASE, (float)medge->crease / 255.0f);
		BM_SetCDf(&bm->edata, e, CD_BWEIGHT, (float)medge->bweight / 255.0f);
	}

	bm->elem_index_dirty &= ~BM_EDGE; /* added in order, clear dirty flag */
	
	if (!me->totpoly) {
		MEM_freeN(vt);
		MEM_freeN(et);
		return;
	}

	mpoly = me->mpoly;
	for (i = 0; i < me->totpoly; i++, mpoly++) {
		BMIter iter;

		BLI_array_empty(fedges);
		BLI_array_empty(verts);

		BLI_array_growitems(fedges, mpoly->totloop);
		BLI_array_growitems(verts, mpoly->totloop);

		for (j = 0; j < mpoly->totloop; j++) {
			ml = &me->mloop[mpoly->loopstart + j];
			v = vt[ml->v];
			e = et[ml->e];

			fedges[j] = e;
			verts[j] = v;
		}
		
		/* not sure what this block is supposed to do,
		 * but its unused. so commenting - campbell */
#if 0
		{
			BMVert *v1, *v2;
			v1 = vt[me->mloop[mpoly->loopstart].v];
			v2 = vt[me->mloop[mpoly->loopstart + 1].v];

			if (v1 == fedges[0]->v1) {
				v2 = fedges[0]->v2;
			}
			else {
				v1 = fedges[0]->v2;
				v2 = fedges[0]->v1;
			}
		}
#endif

		f = BM_Make_Face(bm, verts, fedges, mpoly->totloop, FALSE);

		if (!f) {
			printf("%s: Warning! Bad face in mesh"
			       " \"%s\" at index %d!, skipping\n",
			       __func__, me->id.name + 2, i);
			continue;
		}

		/* dont use 'i' since we may have skipped the face */
		BM_SetIndex(f, bm->totface - 1); /* set_ok */

		/* transfer flag */
		f->head.hflag = BM_Face_Flag_From_MEFlag(mpoly->flag);

		/* this is necassary for selection counts to work properl */
		if (BM_TestHFlag(f, BM_SELECT)) BM_Select(bm, f, TRUE);

		f->mat_nr = mpoly->mat_nr;
		if (i == me->act_face) bm->act_face = f;

		j = 0;
		BM_ITER_INDEX(l, &iter, bm, BM_LOOPS_OF_FACE, f, j) {
			/* Save index of correspsonding MLoop */
			BM_SetIndex(l, mpoly->loopstart + j); /* set_loop */
		}

		/* Copy Custom Dat */
		CustomData_to_bmesh_block(&me->pdata, &bm->pdata, i, &f->head.data);
	}

	bm->elem_index_dirty &= ~BM_FACE; /* added in order, clear dirty flag */

	{
		BMIter fiter;
		BMIter liter;
		
		/* Copy over loop CustomData. Doing this in a separate loop isn't necessary
		 * but is an optimization, to avoid copying a bunch of interpolated customdata
		 * for each BMLoop (from previous BMLoops using the same edge), always followed
		 * by freeing the interpolated data and overwriting it with data from the Mesh. */
		BM_ITER(f, &fiter, bm, BM_FACES_OF_MESH, NULL) {
			BM_ITER(l, &liter, bm, BM_LOOPS_OF_FACE, f) {
				int li = BM_GetIndex(l);
				CustomData_to_bmesh_block(&me->ldata, &bm->ldata, li, &l->head.data);
				BM_SetIndex(l, 0); /* set_loop */
			}
		}
	}

	if (me->mselect && me->totselect != 0) {
		BMIter iter;
		BMVert *vertex;
		BMEdge *edge;
		BMFace *face;
		BMVert **vertex_array = MEM_callocN(sizeof(BMVert *) * bm->totvert,
		                                  "Selection Conversion Vertex Pointer Array");
		BMEdge **edge_array = MEM_callocN(sizeof(BMEdge *) * bm->totedge,
		                                "Selection Conversion Edge Pointer Array");
		BMFace **face_array = MEM_callocN(sizeof(BMFace *) * bm->totface,
		                                "Selection Conversion Face Pointer Array");

		for (i = 0, vertex = BMIter_New(&iter, bm, BM_VERTS_OF_MESH, NULL);
		    vertex; i++, vertex = BMIter_Step(&iter))
		{
			vertex_array[i] = vertex;
		}

		for (i = 0, edge = BMIter_New(&iter, bm, BM_EDGES_OF_MESH, NULL);
		    edge; i++, edge = BMIter_Step(&iter))
		{
			edge_array[i] = edge;
		}

		for (i = 0, face = BMIter_New(&iter, bm, BM_FACES_OF_MESH, NULL);
		    face; i++, face = BMIter_Step(&iter))
		{
			face_array[i] = face;
		}

		if (me->mselect) {
			for (i = 0; i < me->totselect; i++) {
				if (me->mselect[i].type == ME_VSEL) {
					BM_store_selection(bm, vertex_array[me->mselect[i].index]);
				}
				else if (me->mselect[i].type == ME_ESEL) {
					BM_store_selection(bm, edge_array[me->mselect[i].index]);
				}
				else if (me->mselect[i].type == ME_FSEL) {
					BM_store_selection(bm, face_array[me->mselect[i].index]);
				}
			}
		}
		else {
			me->totselect = 0;
		}

		MEM_freeN(vertex_array);
		MEM_freeN(edge_array);
		MEM_freeN(face_array);
	}
	else {
		me->totselect = 0;
		if (me->mselect) {
			MEM_freeN(me->mselect);
			me->mselect = NULL;
		}
	}

	BLI_array_free(fedges);
	BLI_array_free(verts);
	
	MEM_freeN(vt);
	MEM_freeN(et);
}

void object_load_bmesh_exec(BMesh *bm, BMOperator *op)
{
	Object *ob = BMO_Get_Pnt(op, "object");
	/* Scene *scene = BMO_Get_Pnt(op, "scene"); */
	Mesh *me = ob->data;

	BMO_CallOpf(bm, "bmesh_to_mesh mesh=%p object=%p notesselation=%i", me, ob, TRUE);
}


static BMVert **bmesh_to_mesh_vertex_map(BMesh *bm, int ototvert)
{
	BMVert **vertMap = NULL;
	BMVert *eve;
	int index;
	int i = 0;
	BMIter iter;

	/* caller needs to ensure this */
	BLI_assert(ototvert > 0);

	vertMap = MEM_callocN(sizeof(*vertMap)*ototvert, "vertMap");
	if (CustomData_has_layer(&bm->vdata, CD_SHAPE_KEYINDEX)) {
		int *keyi;
		BM_ITER(eve, &iter, bm, BM_VERTS_OF_MESH, NULL) {
			keyi = CustomData_bmesh_get(&bm->vdata, eve->head.data, CD_SHAPE_KEYINDEX);
			if (keyi) {
				if (((index = *keyi) != ORIGINDEX_NONE) && (index < ototvert)) {
					vertMap[index] = eve;
				}
			}
			else {
				if (i < ototvert) {
					vertMap[i] = eve;
				}
			}
			i++;
		}
	}
	else {
		BM_ITER(eve, &iter, bm, BM_VERTS_OF_MESH, NULL) {
			if (i < ototvert) {
				vertMap[i] = eve;
			}
			else {
				break;
			}
			i++;
		}
	}

	return vertMap;
}

BM_INLINE void bmesh_quick_edgedraw_flag(MEdge *med, BMEdge *e)
{
	/* this is a cheap way to set the edge draw, its not precise and will
	 * pick the first 2 faces an edge uses */


	if ( /* (med->flag & ME_EDGEDRAW) && */ /* assume to be true */
	     (e->l && (e->l != e->l->radial_next)) &&
	     (dot_v3v3(e->l->f->no, e->l->radial_next->f->no) > 0.998f))
	{
		med->flag &= ~ME_EDGEDRAW;
	}
}


void bmesh_to_mesh_exec(BMesh *bm, BMOperator *op)
{
	Mesh *me = BMO_Get_Pnt(op, "mesh");
	/* Object *ob = BMO_Get_Pnt(op, "object"); */
	MLoop *mloop;
	MPoly *mpoly;
	MVert *mvert, *oldverts;
	MEdge *med, *medge;
	BMVert *v, *eve;
	BMEdge *e;
	BMLoop *l;
	BMFace *f;
	BMIter iter, liter;
	int i, j, *keyi, ototvert, totloop;
	int dotess = !BMO_Get_Int(op, "notesselation");
	
	ototvert = me->totvert;

	/* new Vertex block */
	if (bm->totvert == 0) mvert = NULL;
	else mvert = MEM_callocN(bm->totvert * sizeof(MVert), "loadeditbMesh vert");

	/* new Edge block */
	if (bm->totedge == 0) medge = NULL;
	else medge = MEM_callocN(bm->totedge * sizeof(MEdge), "loadeditbMesh edge");
	
	/* build ngon dat */
	/* new Ngon Face block */
	if (bm->totface == 0) mpoly = NULL;
	else mpoly = MEM_callocN(bm->totface * sizeof(MPoly), "loadeditbMesh poly");
	
	/* find number of loops to allocat */
	totloop = 0;
	BM_ITER(f, &iter, bm, BM_FACES_OF_MESH, NULL) {
		totloop += f->len;
	}

	if (totloop == 0) mloop = NULL;
	else mloop = MEM_callocN(totloop * sizeof(MLoop), "loadeditbMesh loop");

	/* lets save the old verts just in case we are actually working on
	 * a key ... we now do processing of the keys at the end */
	oldverts = me->mvert;

	/* don't free this yet */
	CustomData_set_layer(&me->vdata, CD_MVERT, NULL);

	/* free custom data */
	CustomData_free(&me->vdata, me->totvert);
	CustomData_free(&me->edata, me->totedge);
	CustomData_free(&me->fdata, me->totface);
	CustomData_free(&me->ldata, me->totloop);
	CustomData_free(&me->pdata, me->totpoly);

	/* add new custom data */
	me->totvert = bm->totvert;
	me->totedge = bm->totedge;
	me->totloop = totloop;
	me->totpoly = bm->totface;
	/* will be overwritten with a valid value if 'dotess' is set, otherwise we
	 * end up with 'me->totface' and me->mface == NULL which can crash [#28625]
	 */
	me->totface = 0;

	CustomData_copy(&bm->vdata, &me->vdata, CD_MASK_MESH, CD_CALLOC, me->totvert);
	CustomData_copy(&bm->edata, &me->edata, CD_MASK_MESH, CD_CALLOC, me->totedge);
	CustomData_copy(&bm->ldata, &me->ldata, CD_MASK_MESH, CD_CALLOC, me->totloop);
	CustomData_copy(&bm->pdata, &me->pdata, CD_MASK_MESH, CD_CALLOC, me->totpoly);

	CustomData_add_layer(&me->vdata, CD_MVERT, CD_ASSIGN, mvert, me->totvert);
	CustomData_add_layer(&me->edata, CD_MEDGE, CD_ASSIGN, medge, me->totedge);
	CustomData_add_layer(&me->ldata, CD_MLOOP, CD_ASSIGN, mloop, me->totloop);
	CustomData_add_layer(&me->pdata, CD_MPOLY, CD_ASSIGN, mpoly, me->totpoly);

	/* this is called again, 'dotess' arg is used there */
	mesh_update_customdata_pointers(me, 0);
	
	i = 0;
	BM_ITER(v, &iter, bm, BM_VERTS_OF_MESH, NULL) {
		float *bweight = CustomData_bmesh_get(&bm->vdata, v->head.data, CD_BWEIGHT);

		mvert->bweight = bweight ? (char)((*bweight) * 255) : 0;

		copy_v3_v3(mvert->co, v->co);
		normal_float_to_short_v3(mvert->no, v->no);
		
		mvert->flag = BM_Vert_Flag_To_MEFlag(v);

		BM_SetIndex(v, i); /* set_inline */

		/* copy over customdat */
		CustomData_from_bmesh_block(&bm->vdata, &me->vdata, v->head.data, i);

		i++;
		mvert++;

		BM_CHECK_ELEMENT(bm, v);
	}
	bm->elem_index_dirty &= ~BM_VERT;

	med = medge;
	i = 0;
	BM_ITER(e, &iter, bm, BM_EDGES_OF_MESH, NULL) {
		float *crease = CustomData_bmesh_get(&bm->edata, e->head.data, CD_CREASE);
		float *bweight = CustomData_bmesh_get(&bm->edata, e->head.data, CD_BWEIGHT);
		
		med->v1 = BM_GetIndex(e->v1);
		med->v2 = BM_GetIndex(e->v2);
		med->crease = crease ? (char)((*crease) * 255) : 0;
		med->bweight = bweight ? (char)((*bweight) * 255) : 0;
		
		med->flag = BM_Edge_Flag_To_MEFlag(e);
		
		BM_SetIndex(e, i); /* set_inline */

		/* copy over customdat */
		CustomData_from_bmesh_block(&bm->edata, &me->edata, e->head.data, i);

		bmesh_quick_edgedraw_flag(med, e);

		i++;
		med++;
		BM_CHECK_ELEMENT(bm, e);
	}
	bm->elem_index_dirty &= ~BM_EDGE;

	i = 0;
	j = 0;
	BM_ITER(f, &iter, bm, BM_FACES_OF_MESH, NULL) {
		mpoly->loopstart = j;
		mpoly->totloop = f->len;
		mpoly->mat_nr = f->mat_nr;
		mpoly->flag = BM_Face_Flag_To_MEFlag(f);

		l = BMIter_New(&liter, bm, BM_LOOPS_OF_FACE, f);
		for ( ; l; l = BMIter_Step(&liter), j++, mloop++) {
			mloop->e = BM_GetIndex(l->e);
			mloop->v = BM_GetIndex(l->v);

			/* copy over customdat */
			CustomData_from_bmesh_block(&bm->ldata, &me->ldata, l->head.data, j);
			BM_CHECK_ELEMENT(bm, l);
			BM_CHECK_ELEMENT(bm, l->e);
			BM_CHECK_ELEMENT(bm, l->v);
		}
		
		if (f == bm->act_face) me->act_face = i;

		/* copy over customdat */
		CustomData_from_bmesh_block(&bm->pdata, &me->pdata, f->head.data, i);

		i++;
		mpoly++;
		BM_CHECK_ELEMENT(bm, f);
	}

	/* patch hook indices and vertex parents */
	if (ototvert > 0) {
		Object *ob;
		ModifierData *md;
		BMVert **vertMap = NULL;
		int i, j;

		for (ob = G.main->object.first; ob; ob = ob->id.next) {
			if (ob->parent == bm->ob && ELEM(ob->partype, PARVERT1, PARVERT3)) {

				if (vertMap == NULL) {
					vertMap = bmesh_to_mesh_vertex_map(bm, ototvert);
				}

				if (ob->par1 < ototvert) {
					eve = vertMap[ob->par1];
					if (eve) ob->par1 = BM_GetIndex(eve);
				}
				if (ob->par2 < ototvert) {
					eve = vertMap[ob->par2];
					if (eve) ob->par2 = BM_GetIndex(eve);
				}
				if (ob->par3 < ototvert) {
					eve = vertMap[ob->par3];
					if (eve) ob->par3 = BM_GetIndex(eve);
				}
				
			}
			if (ob->data == me) {
				for (md = ob->modifiers.first; md; md = md->next) {
					if (md->type == eModifierType_Hook) {
						HookModifierData *hmd = (HookModifierData *) md;

						if (vertMap == NULL) {
							vertMap = bmesh_to_mesh_vertex_map(bm, ototvert);
						}
						
						for (i = j = 0; i < hmd->totindex; i++) {
							if (hmd->indexar[i] < ototvert) {
								eve = vertMap[hmd->indexar[i]];
								
								if (eve) {
									hmd->indexar[j++] = BM_GetIndex(eve);
								}
							}
							else j++;
						}

						hmd->totindex = j;
					}
				}
			}
		}

		if (vertMap) MEM_freeN(vertMap);
	}

	if (dotess) {
		BKE_mesh_calc_tessface(me);
	}

	mesh_update_customdata_pointers(me, dotess);

	{
		BMEditSelection *selected;
		me->totselect = BLI_countlist(&(bm->selected));

		if (me->mselect) MEM_freeN(me->mselect);

		me->mselect = MEM_callocN(sizeof(MSelect) * me->totselect, "Mesh selection history");


		for (i = 0, selected = bm->selected.first; selected; i++, selected = selected->next) {
			if (selected->htype == BM_VERT) {
				me->mselect[i].type = ME_VSEL;

			}
			else if (selected->htype == BM_EDGE) {
				me->mselect[i].type = ME_ESEL;

			}
			else if (selected->htype == BM_FACE) {
				me->mselect[i].type = ME_FSEL;
			}

			me->mselect[i].index = BM_GetIndex(selected->data);
		}
	}

	/* see comment below, this logic is in twice */

	if (me->key) {
		KeyBlock *currkey;
		KeyBlock *actkey = BLI_findlink(&me->key->block, bm->shapenr - 1);

		float (*ofs)[3] = NULL;

		/* go through and find any shapekey customdata layers
		 * that might not have corrusponding KeyBlocks, and add them if
		 * necassary */
		j = 0;
		for (i = 0; i < bm->vdata.totlayer; i++) {
			if (bm->vdata.layers[i].type != CD_SHAPEKEY)
				continue;

			for (currkey = me->key->block.first; currkey; currkey = currkey->next) {
				if (currkey->uid == bm->vdata.layers[i].uid)
					break;
			}
			
			if (!currkey) {
				currkey = MEM_callocN(sizeof(KeyBlock), "KeyBlock mesh_conv.c");
				currkey->type = KEY_LINEAR;
				currkey->slidermin = 0.0f;
				currkey->slidermax = 1.0f;

				BLI_addtail(&me->key->block, currkey);
				me->key->totkey++;
			}

			j++;
		}


		/* editing the base key should update others */
		if (me->key->type == KEY_RELATIVE && oldverts) {
			int act_is_basis = 0;
			/* find if this key is a basis for any others */
			for (currkey = me->key->block.first; currkey; currkey = currkey->next) {
				if (bm->shapenr - 1 == currkey->relative) {
					act_is_basis = 1;
					break;
				}
			}

			if (act_is_basis) { /* active key is a base */
				float (*fp)[3] = actkey->data;
				int *keyi;
				i = 0;
				ofs = MEM_callocN(sizeof(float) * 3 * bm->totvert,  "currkey->data");
				mvert = me->mvert;
				BM_ITER(eve, &iter, bm, BM_VERTS_OF_MESH, NULL) {
					keyi = CustomData_bmesh_get(&bm->vdata, eve->head.data, CD_SHAPE_KEYINDEX);
					if (keyi && *keyi != ORIGINDEX_NONE) {
						sub_v3_v3v3(ofs[i], mvert->co, fp[*keyi]);
					}
					i++;
					mvert++;
				}
			}
		}


		for (currkey = me->key->block.first; currkey; currkey = currkey->next) {
			j = 0;

			for (i = 0; i < bm->vdata.totlayer; i++) {
				if (bm->vdata.layers[i].type != CD_SHAPEKEY)
					continue;

				if (currkey->uid == bm->vdata.layers[i].uid) {
					int apply_offset = (ofs && (currkey != actkey) && (bm->shapenr - 1 == currkey->relative));
					float *fp, *co;
					float (*ofs_pt)[3] = ofs;

					if (currkey->data)
						MEM_freeN(currkey->data);
					currkey->data = fp = MEM_mallocN(sizeof(float) * 3 * bm->totvert, "shape key data");
					currkey->totelem = bm->totvert;

					BM_ITER(eve, &iter, bm, BM_VERTS_OF_MESH, NULL) {
						co = currkey == actkey ? eve->co : CustomData_bmesh_get_n(&bm->vdata, eve->head.data, CD_SHAPEKEY, j);
						
						copy_v3_v3(fp, co);

						/* propagate edited basis offsets to other shapes */
						if (apply_offset) {
							add_v3_v3(fp, *ofs_pt++);
						}

						fp += 3;
					}
					break;
				}

				j++;
			}

			/* if we didn't find a shapekey, tag the block to be reconstructed
			 * via the old method below */
			if (j == CustomData_number_of_layers(&bm->vdata, CD_SHAPEKEY)) {
				currkey->flag |= KEYBLOCK_MISSING;
			}
		}

		if (ofs) MEM_freeN(ofs);
	}

	/* XXX, code below is from trunk and a duplicate functionality
	 * to the block above.
	 * We should use one or the other, having both means we have to maintain
	 * both and keep them working the same way which is a hassle - campbell */

	/* old method of reconstructing keys via vertice's original key indices,
	 * currently used if the new method above fails (which is theoretically
	 * possible in certain cases of undo) */
	if (me->key) {
		float *fp, *newkey, *oldkey;
		KeyBlock *currkey;
		KeyBlock *actkey = BLI_findlink(&me->key->block, bm->shapenr - 1);

		float (*ofs)[3] = NULL;

		/* editing the base key should update others */
		if (me->key->type == KEY_RELATIVE && oldverts) {
			int act_is_basis = 0;
			/* find if this key is a basis for any others */
			for (currkey = me->key->block.first; currkey; currkey = currkey->next) {
				if (bm->shapenr - 1 == currkey->relative) {
					act_is_basis = 1;
					break;
				}
			}

			if (act_is_basis) { /* active key is a base */
				float (*fp)[3] = actkey->data;
				int *keyi;
				i = 0;
				ofs = MEM_callocN(sizeof(float) * 3 * bm->totvert,  "currkey->data");
				mvert = me->mvert;
				BM_ITER(eve, &iter, bm, BM_VERTS_OF_MESH, NULL) {
					keyi = CustomData_bmesh_get(&bm->vdata, eve->head.data, CD_SHAPE_KEYINDEX);
					if (keyi && *keyi != ORIGINDEX_NONE) {
						sub_v3_v3v3(ofs[i], mvert->co, fp[*keyi]);
					}
					i++;
					mvert++;
				}
			}
		}

		/* Lets reorder the key data so that things line up roughly
		 * with the way things were before editmode */
		currkey = me->key->block.first;
		while (currkey) {
			int apply_offset = (ofs && (currkey != actkey) && (bm->shapenr - 1 == currkey->relative));

			if (!(currkey->flag & KEYBLOCK_MISSING)) {
				currkey = currkey->next;
				continue;
			}
			
			printf("warning: had to hackishly reconstruct shape key \"%s\","
			       " it may not be correct anymore.\n", currkey->name);

			currkey->flag &= ~KEYBLOCK_MISSING;

			fp = newkey = MEM_callocN(me->key->elemsize * bm->totvert,  "currkey->data");
			oldkey = currkey->data;

			eve = BMIter_New(&iter, bm, BM_VERTS_OF_MESH, NULL);

			i = 0;
			mvert = me->mvert;
			while (eve) {
				keyi = CustomData_bmesh_get(&bm->vdata, eve->head.data, CD_SHAPE_KEYINDEX);
				if (!keyi) {
					break;
				}
				if (*keyi >= 0 && *keyi < currkey->totelem) { // valid old vertex
					if (currkey == actkey) {
						if (actkey == me->key->refkey) {
							copy_v3_v3(fp, mvert->co);
						}
						else {
							copy_v3_v3(fp, mvert->co);
							if (oldverts) {
								copy_v3_v3(mvert->co, oldverts[*keyi].co);
							}
						}
					}
					else {
						if (oldkey) {
							copy_v3_v3(fp, oldkey + 3 * *keyi);
						}
					}
				}
				else {
					copy_v3_v3(fp, mvert->co);
				}

				/* propagate edited basis offsets to other shapes */
				if (apply_offset) {
					add_v3_v3(fp, ofs[i]);
				}

				fp+= 3;
				++i;
				++mvert;
				eve = BMIter_Step(&iter);
			}
			currkey->totelem = bm->totvert;
			if (currkey->data) MEM_freeN(currkey->data);
			currkey->data = newkey;
			
			currkey = currkey->next;
		}

		if (ofs) MEM_freeN(ofs);
	}

	if (oldverts) MEM_freeN(oldverts);
}
