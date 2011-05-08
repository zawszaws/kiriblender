 /*
* $Id$
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
* along with this program; if not, write to the Free Software  Foundation,
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
* The Original Code is Copyright (C) 2006 Blender Foundation.
* All rights reserved.
*
* The Original Code is: all of this file.
*
* Contributor(s): Ben Batt <benbatt@gmail.com>
*
* ***** END GPL LICENSE BLOCK *****
*
* Implementation of CDDerivedMesh.
*
* BKE_cdderivedmesh.h contains the function prototypes for this file.
*
*/

/** \file blender/blenkernel/intern/cdderivedmesh.c
 *  \ingroup bke
 */
 

/* TODO maybe BIF_gl.h should include string.h? */
#include <string.h>
#include "BIF_gl.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_paint.h"
#include "BKE_utildefines.h"
#include "BKE_tessmesh.h"

#include "BLI_editVert.h"
#include "BLI_scanfill.h"
#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_edgehash.h"
#include "BLI_editVert.h"
#include "BLI_math.h"
#include "BLI_pbvh.h"
#include "BLI_array.h"
#include "BLI_smallhash.h"
#include "BLI_utildefines.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_paint.h"


#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_curve_types.h" /* for Curve */

#include "MEM_guardedalloc.h"

#include "GPU_buffers.h"
#include "GPU_draw.h"
#include "GPU_extensions.h"
#include "GPU_material.h"

#include <string.h>
#include <limits.h>
#include <math.h>

typedef struct {
	DerivedMesh dm;

	/* these point to data in the DerivedMesh custom data layers,
	   they are only here for efficiency and convenience **/
	MVert *mvert;
	MEdge *medge;
	MFace *mface;
	MLoop *mloop;
	MPoly *mpoly;

	/* Cached */
	struct PBVH *pbvh;
	int pbvh_draw;

	/* Mesh connectivity */
	struct ListBase *fmap;
	struct IndexNode *fmap_mem;
} CDDerivedMesh;

DMFaceIter *cdDM_newFaceIter(DerivedMesh *source);

/**************** DerivedMesh interface functions ****************/
static int cdDM_getNumVerts(DerivedMesh *dm)
{
	return dm->numVertData;
}

static int cdDM_getNumEdges(DerivedMesh *dm)
{
	return dm->numEdgeData;
}

static int cdDM_getNumTessFaces(DerivedMesh *dm)
{
	return dm->numFaceData;
}

static int cdDM_getNumFaces(DerivedMesh *dm)
{
	return dm->numPolyData;
}

static void cdDM_getVert(DerivedMesh *dm, int index, MVert *vert_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	*vert_r = cddm->mvert[index];
}

static void cdDM_getEdge(DerivedMesh *dm, int index, MEdge *edge_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	*edge_r = cddm->medge[index];
}

static void cdDM_getFace(DerivedMesh *dm, int index, MFace *face_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	*face_r = cddm->mface[index];
}

static void cdDM_copyVertArray(DerivedMesh *dm, MVert *vert_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	memcpy(vert_r, cddm->mvert, sizeof(*vert_r) * dm->numVertData);
}

static void cdDM_copyEdgeArray(DerivedMesh *dm, MEdge *edge_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	memcpy(edge_r, cddm->medge, sizeof(*edge_r) * dm->numEdgeData);
}

static void cdDM_copyFaceArray(DerivedMesh *dm, MFace *face_r)
{
	CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
	memcpy(face_r, cddm->mface, sizeof(*face_r) * dm->numFaceData);
}

static void cdDM_getMinMax(DerivedMesh *dm, float min_r[3], float max_r[3])
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	int i;

	if (dm->numVertData) {
		for (i=0; i<dm->numVertData; i++) {
			DO_MINMAX(cddm->mvert[i].co, min_r, max_r);
		}
	} else {
		min_r[0] = min_r[1] = min_r[2] = max_r[0] = max_r[1] = max_r[2] = 0.0;
	}
}

static void cdDM_getVertCo(DerivedMesh *dm, int index, float co_r[3])
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;

	VECCOPY(co_r, cddm->mvert[index].co);
}

static void cdDM_getVertCos(DerivedMesh *dm, float (*cos_r)[3])
{
	MVert *mv = CDDM_get_verts(dm);
	int i;

	for(i = 0; i < dm->numVertData; i++, mv++)
		VECCOPY(cos_r[i], mv->co);
}

static void cdDM_getVertNo(DerivedMesh *dm, int index, float no_r[3])
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	normal_short_to_float_v3(no_r, cddm->mvert[index].no);
}

static ListBase *cdDM_getFaceMap(Object *ob, DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;

	if(!cddm->fmap && ob->type == OB_MESH) {
		Mesh *me= ob->data;

		create_vert_face_map(&cddm->fmap, &cddm->fmap_mem, me->mface,
					 me->totvert, me->totface);
	}

	return cddm->fmap;
}

static int can_pbvh_draw(Object *ob, DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	Mesh *me= ob->data;
	int deformed= 0;

	/* active modifiers means extra deformation, which can't be handled correct
	   on bith of PBVH and sculpt "layer" levels, so use PBVH only for internal brush
	   stuff and show final DerivedMesh so user would see actual object shape */
	deformed|= ob->sculpt->modifiers_active;

	/* as in case with modifiers, we can't synchronize deformation made against
	   PBVH and non-locked keyblock, so also use PBVH only for brushes and
	   final DM to give final result to user */
	deformed|= ob->sculpt->kb && (ob->shapeflag&OB_SHAPE_LOCK) == 0;

	if(deformed)
		return 0;

	return (cddm->mvert == me->mvert) || ob->sculpt->kb;
}

static struct PBVH *cdDM_getPBVH(Object *ob, DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;

	if(!ob) {
		cddm->pbvh= NULL;
		return NULL;
	}

	if(!ob->sculpt)
		return NULL;
	if(ob->sculpt->pbvh) {
		cddm->pbvh= ob->sculpt->pbvh;
		cddm->pbvh_draw = can_pbvh_draw(ob, dm);
	}

	/* always build pbvh from original mesh, and only use it for drawing if
	   this derivedmesh is just original mesh. it's the multires subsurf dm
	   that this is actually for, to support a pbvh on a modified mesh */
	if(!cddm->pbvh && ob->type == OB_MESH) {
		SculptSession *ss= ob->sculpt;
		Mesh *me= ob->data;
		cddm->pbvh = BLI_pbvh_new();
		cddm->pbvh_draw = can_pbvh_draw(ob, dm);
		BLI_pbvh_build_mesh(cddm->pbvh, me->mface, me->mvert,
				   me->totface, me->totvert);

		if(ss->modifiers_active && ob->derivedDeform) {
			DerivedMesh *deformdm= ob->derivedDeform;
			float (*vertCos)[3];
			int totvert;

			totvert= deformdm->getNumVerts(deformdm);
			vertCos= MEM_callocN(3*totvert*sizeof(float), "cdDM_getPBVH vertCos");
			deformdm->getVertCos(deformdm, vertCos);
			BLI_pbvh_apply_vertCos(cddm->pbvh, vertCos);
			MEM_freeN(vertCos);
		}
	}

	return cddm->pbvh;
}

/* update vertex normals so that drawing smooth faces works during sculpt
   TODO: proper fix is to support the pbvh in all drawing modes */
static void cdDM_update_normals_from_pbvh(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	float (*face_nors)[3];

	if(!cddm->pbvh || !cddm->pbvh_draw || !dm->numFaceData)
		return;

	face_nors = CustomData_get_layer(&dm->faceData, CD_NORMAL);

	BLI_pbvh_update(cddm->pbvh, PBVH_UpdateNormals, face_nors);
}

static void cdDM_drawVerts(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mv = cddm->mvert;
	int i;

	if( GPU_buffer_legacy(dm) ) {
		glBegin(GL_POINTS);
		for(i = 0; i < dm->numVertData; i++, mv++)
			glVertex3fv(mv->co);
		glEnd();
	}
	else {	/* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		GPU_vertex_setup(dm);
		if( !GPU_buffer_legacy(dm) ) {
			if(dm->drawObject->nelements)	glDrawArrays(GL_POINTS,0, dm->drawObject->nelements);
			else							glDrawArrays(GL_POINTS,0, dm->drawObject->nlooseverts);
		}
		GPU_buffer_unbind();
	}
}

static void cdDM_drawUVEdges(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MFace *mf = cddm->mface;
	MTFace *tf = DM_get_tessface_data_layer(dm, CD_MTFACE);
	int i;

	if(mf) {
		if( GPU_buffer_legacy(dm) ) {
			glBegin(GL_LINES);
			for(i = 0; i < dm->numFaceData; i++, mf++, tf++) {
				if(!(mf->flag&ME_HIDE)) {
					glVertex2fv(tf->uv[0]);
					glVertex2fv(tf->uv[1]);

					glVertex2fv(tf->uv[1]);
					glVertex2fv(tf->uv[2]);

					if(!mf->v4) {
						glVertex2fv(tf->uv[2]);
						glVertex2fv(tf->uv[0]);
					} else {
						glVertex2fv(tf->uv[2]);
						glVertex2fv(tf->uv[3]);

						glVertex2fv(tf->uv[3]);
						glVertex2fv(tf->uv[0]);
					}
				}
			}
			glEnd();
		}
		else {
			int prevstart = 0;
			int prevdraw = 1;
			int draw = 1;
			int curpos = 0;

			GPU_uvedge_setup(dm);
			if( !GPU_buffer_legacy(dm) ) {
				for(i = 0; i < dm->numFaceData; i++, mf++) {
					if(!(mf->flag&ME_HIDE)) {
						draw = 1;
					} 
					else {
						draw = 0;
					}
					if( prevdraw != draw ) {
						if( prevdraw > 0 && (curpos-prevstart) > 0) {
							glDrawArrays(GL_LINES,prevstart,curpos-prevstart);
						}
						prevstart = curpos;
					}
					if( mf->v4 ) {
						curpos += 8;
					}
					else {
						curpos += 6;
					}
					prevdraw = draw;
				}
				if( prevdraw > 0 && (curpos-prevstart) > 0 ) {
					glDrawArrays(GL_LINES,prevstart,curpos-prevstart);
				}
			}
			GPU_buffer_unbind();
		}
	}
}

static void cdDM_drawEdges(DerivedMesh *dm, int drawLooseEdges, int drawAllEdges)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mvert = cddm->mvert;
	MEdge *medge = cddm->medge;
	int i;
	
	if( GPU_buffer_legacy(dm) ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawEdges\n" );
		glBegin(GL_LINES);
		for(i = 0; i < dm->numEdgeData; i++, medge++) {
			if((drawAllEdges || (medge->flag&ME_EDGEDRAW))
			   && (drawLooseEdges || !(medge->flag&ME_LOOSEEDGE))) {
				glVertex3fv(mvert[medge->v1].co);
				glVertex3fv(mvert[medge->v2].co);
			}
		}
		glEnd();
	}
	else {	/* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		int prevstart = 0;
		int prevdraw = 1;
		int draw = 1;

		GPU_edge_setup(dm);
		if( !GPU_buffer_legacy(dm) ) {
			for(i = 0; i < dm->numEdgeData; i++, medge++) {
				if((drawAllEdges || (medge->flag&ME_EDGEDRAW))
				   && (drawLooseEdges || !(medge->flag&ME_LOOSEEDGE))) {
					draw = 1;
				} 
				else {
					draw = 0;
				}
				if( prevdraw != draw ) {
					if( prevdraw > 0 && (i-prevstart) > 0 ) {
						GPU_buffer_draw_elements( dm->drawObject->edges, GL_LINES, prevstart*2, (i-prevstart)*2  );
					}
					prevstart = i;
				}
				prevdraw = draw;
			}
			if( prevdraw > 0 && (i-prevstart) > 0 ) {
				GPU_buffer_draw_elements( dm->drawObject->edges, GL_LINES, prevstart*2, (i-prevstart)*2  );
			}
		}
		GPU_buffer_unbind();
	}
}

static void cdDM_drawLooseEdges(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mvert = cddm->mvert;
	MEdge *medge = cddm->medge;
	int i;

	if( GPU_buffer_legacy(dm) ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawLooseEdges\n" );
		glBegin(GL_LINES);
		for(i = 0; i < dm->numEdgeData; i++, medge++) {
			if(medge->flag&ME_LOOSEEDGE) {
				glVertex3fv(mvert[medge->v1].co);
				glVertex3fv(mvert[medge->v2].co);
			}
		}
		glEnd();
	}
	else {	/* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		int prevstart = 0;
		int prevdraw = 1;
		int draw = 1;

		GPU_edge_setup(dm);
		if( !GPU_buffer_legacy(dm) ) {
			for(i = 0; i < dm->numEdgeData; i++, medge++) {
				if(medge->flag&ME_LOOSEEDGE) {
					draw = 1;
				} 
				else {
					draw = 0;
				}
				if( prevdraw != draw ) {
					if( prevdraw > 0 && (i-prevstart) > 0) {
						GPU_buffer_draw_elements( dm->drawObject->edges, GL_LINES, prevstart*2, (i-prevstart)*2  );
					}
					prevstart = i;
				}
				prevdraw = draw;
			}
			if( prevdraw > 0 && (i-prevstart) > 0 ) {
				GPU_buffer_draw_elements( dm->drawObject->edges, GL_LINES, prevstart*2, (i-prevstart)*2  );
			}
		}
		GPU_buffer_unbind();
	}
}

static void cdDM_drawFacesSolid(DerivedMesh *dm,
				float (*partial_redraw_planes)[4],
				int UNUSED(fast), int (*setMaterial)(int, void *attribs))
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mvert = cddm->mvert;
	MFace *mface = cddm->mface;
	float *nors= dm->getTessFaceDataArray(dm, CD_NORMAL);
	int a, glmode = -1, shademodel = -1, matnr = -1, drawCurrentMat = 1;

#define PASSVERT(index) {						\
	if(shademodel == GL_SMOOTH) {				\
		short *no = mvert[index].no;			\
		glNormal3sv(no);						\
	}											\
	glVertex3fv(mvert[index].co);	\
}

	if(cddm->pbvh && cddm->pbvh_draw) {
		if(dm->numFaceData) {
			float (*face_nors)[3] = CustomData_get_layer(&dm->faceData, CD_NORMAL);

			/* should be per face */
			if(!setMaterial(mface->mat_nr+1, NULL))
				return;

			glShadeModel((mface->flag & ME_SMOOTH)? GL_SMOOTH: GL_FLAT);
			BLI_pbvh_draw(cddm->pbvh, partial_redraw_planes, face_nors, (mface->flag & ME_SMOOTH));
			glShadeModel(GL_FLAT);
		}

		return;
	}

	if( GPU_buffer_legacy(dm) ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawFacesSolid\n" );
		glBegin(glmode = GL_QUADS);
		for(a = 0; a < dm->numFaceData; a++, mface++) {
			int new_glmode, new_matnr, new_shademodel;

			new_glmode = mface->v4?GL_QUADS:GL_TRIANGLES;
			new_matnr = mface->mat_nr + 1;
			new_shademodel = (mface->flag & ME_SMOOTH)?GL_SMOOTH:GL_FLAT;
			
			if(new_glmode != glmode || new_matnr != matnr
			   || new_shademodel != shademodel) {
				glEnd();

				drawCurrentMat = setMaterial(matnr = new_matnr, NULL);

				glShadeModel(shademodel = new_shademodel);
				glBegin(glmode = new_glmode);
			} 
			
			if(drawCurrentMat) {
				if(shademodel == GL_FLAT) {
					if (nors) {
						glNormal3fv(nors);
					}
					else {
						/* TODO make this better (cache facenormals as layer?) */
						float nor[3];
						if(mface->v4) {
							normal_quad_v3( nor,mvert[mface->v1].co, mvert[mface->v2].co, mvert[mface->v3].co, mvert[mface->v4].co);
						} else {
							normal_tri_v3( nor,mvert[mface->v1].co, mvert[mface->v2].co, mvert[mface->v3].co);
						}
						glNormal3fv(nor);
					}
				}

				PASSVERT(mface->v1);
				PASSVERT(mface->v2);
				PASSVERT(mface->v3);
				if(mface->v4) {
					PASSVERT(mface->v4);
				}
			}

			if(nors) nors += 3;
		}
		glEnd();
	}
	else {	/* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		GPU_vertex_setup( dm );
		GPU_normal_setup( dm );
		if( !GPU_buffer_legacy(dm) ) {
			glShadeModel(GL_SMOOTH);
			for( a = 0; a < dm->drawObject->nmaterials; a++ ) {
				if( setMaterial(dm->drawObject->materials[a].mat_nr+1, NULL) )
					glDrawArrays(GL_TRIANGLES, dm->drawObject->materials[a].start, dm->drawObject->materials[a].end-dm->drawObject->materials[a].start);
			}
		}
		GPU_buffer_unbind( );
	}

#undef PASSVERT
	glShadeModel(GL_FLAT);
}

static void cdDM_drawFacesColored(DerivedMesh *dm, int useTwoSided, unsigned char *col1, unsigned char *col2)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	int a, glmode;
	unsigned char *cp1, *cp2;
	MVert *mvert = cddm->mvert;
	MFace *mface = cddm->mface;

	cp1 = col1;
	if(col2) {
		cp2 = col2;
	} else {
		cp2 = NULL;
		useTwoSided = 0;
	}

	/* there's a conflict here... twosided colors versus culling...? */
	/* defined by history, only texture faces have culling option */
	/* we need that as mesh option builtin, next to double sided lighting */
	if(col2) {
		glEnable(GL_CULL_FACE);
	}

	cdDM_update_normals_from_pbvh(dm);

	if( GPU_buffer_legacy(dm) ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawFacesColored\n" );
		glShadeModel(GL_SMOOTH);
		glBegin(glmode = GL_QUADS);
		for(a = 0; a < dm->numFaceData; a++, mface++, cp1 += 16) {
			int new_glmode = mface->v4?GL_QUADS:GL_TRIANGLES;

			if(new_glmode != glmode) {
				glEnd();
				glBegin(glmode = new_glmode);
			}
				
			glColor3ubv(cp1+0);
			glVertex3fv(mvert[mface->v1].co);
			glColor3ubv(cp1+4);
			glVertex3fv(mvert[mface->v2].co);
			glColor3ubv(cp1+8);
			glVertex3fv(mvert[mface->v3].co);
			if(mface->v4) {
				glColor3ubv(cp1+12);
				glVertex3fv(mvert[mface->v4].co);
			}
				
			if(useTwoSided) {
				glColor3ubv(cp2+8);
				glVertex3fv(mvert[mface->v3].co );
				glColor3ubv(cp2+4);
				glVertex3fv(mvert[mface->v2].co );
				glColor3ubv(cp2+0);
				glVertex3fv(mvert[mface->v1].co );
				if(mface->v4) {
					glColor3ubv(cp2+12);
					glVertex3fv(mvert[mface->v4].co );
				}
			}
			if(col2) cp2 += 16;
		}
		glEnd();
	}
	else { /* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		GPU_color4_upload(dm,cp1);
		GPU_vertex_setup(dm);
		GPU_color_setup(dm);
		if( !GPU_buffer_legacy(dm) ) {
			glShadeModel(GL_SMOOTH);
			glDrawArrays(GL_TRIANGLES, 0, dm->drawObject->nelements);

			if( useTwoSided ) {
				GPU_color4_upload(dm,cp2);
				GPU_color_setup(dm);
				glCullFace(GL_FRONT);
				glDrawArrays(GL_TRIANGLES, 0, dm->drawObject->nelements);
				glCullFace(GL_BACK);
			}
		}
		GPU_buffer_unbind();
	}

	glShadeModel(GL_FLAT);
	glDisable(GL_CULL_FACE);
}

static void cdDM_drawFacesTex_common(DerivedMesh *dm,
               int (*drawParams)(MTFace *tface, int has_vcol, int matnr),
               int (*drawParamsMapped)(void *userData, int index),
               void *userData) 
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mv = cddm->mvert;
	MFace *mf = DM_get_tessface_data_layer(dm, CD_MFACE);
	MCol *realcol = dm->getTessFaceDataArray(dm, CD_TEXTURE_MCOL);
	float *nors= dm->getTessFaceDataArray(dm, CD_NORMAL);
	MTFace *tf = DM_get_tessface_data_layer(dm, CD_MTFACE);
	int i, j, orig, *index = DM_get_tessface_data_layer(dm, CD_ORIGINDEX);
	int startFace = 0, lastFlag = 0xdeadbeef;
	MCol *mcol = dm->getTessFaceDataArray(dm, CD_WEIGHT_MCOL);
	if(!mcol)
		mcol = dm->getTessFaceDataArray(dm, CD_MCOL);

	cdDM_update_normals_from_pbvh(dm);

	if( GPU_buffer_legacy(dm) ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawFacesTex_common\n" );
		for(i = 0; i < dm->numFaceData; i++, mf++) {
			MVert *mvert;
			int flag;
			unsigned char *cp = NULL;

			if(drawParams) {
				flag = drawParams(tf? &tf[i]: NULL, mcol!=NULL, mf->mat_nr);
			}
			else {
				if(index) {
					orig = *index++;
					if(orig == ORIGINDEX_NONE)		{ if(nors) nors += 3; continue; }
					if(drawParamsMapped) flag = drawParamsMapped(userData, orig);
					else	{ if(nors) nors += 3; continue; }
				}
				else
					if(drawParamsMapped) flag = drawParamsMapped(userData, i);
					else	{ if(nors) nors += 3; continue; }
			}
			
			if(flag != 0) {
				if (flag==1 && mcol)
					cp= (unsigned char*) &mcol[i*4];

				if(!(mf->flag&ME_SMOOTH)) {
					if (nors) {
						glNormal3fv(nors);
					}
					else {
						float nor[3];
						if(mf->v4) {
							normal_quad_v3( nor,mv[mf->v1].co, mv[mf->v2].co, mv[mf->v3].co, mv[mf->v4].co);
						} else {
							normal_tri_v3( nor,mv[mf->v1].co, mv[mf->v2].co, mv[mf->v3].co);
						}
						glNormal3fv(nor);
					}
				}

				glBegin(mf->v4?GL_QUADS:GL_TRIANGLES);
				if(tf) glTexCoord2fv(tf[i].uv[0]);
				if(cp) glColor3ub(cp[3], cp[2], cp[1]);
				mvert = &mv[mf->v1];
				if(mf->flag&ME_SMOOTH) glNormal3sv(mvert->no);
				glVertex3fv(mvert->co);
					
				if(tf) glTexCoord2fv(tf[i].uv[1]);
				if(cp) glColor3ub(cp[7], cp[6], cp[5]);
				mvert = &mv[mf->v2];
				if(mf->flag&ME_SMOOTH) glNormal3sv(mvert->no);
				glVertex3fv(mvert->co);

				if(tf) glTexCoord2fv(tf[i].uv[2]);
				if(cp) glColor3ub(cp[11], cp[10], cp[9]);
				mvert = &mv[mf->v3];
				if(mf->flag&ME_SMOOTH) glNormal3sv(mvert->no);
				glVertex3fv(mvert->co);

				if(mf->v4) {
					if(tf) glTexCoord2fv(tf[i].uv[3]);
					if(cp) glColor3ub(cp[15], cp[14], cp[13]);
					mvert = &mv[mf->v4];
					if(mf->flag&ME_SMOOTH) glNormal3sv(mvert->no);
					glVertex3fv(mvert->co);
				}
				glEnd();
			}
			
			if(nors) nors += 3;
		}
	} else { /* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		MCol *col = realcol;
		if(!col)
			col = mcol;

		GPU_vertex_setup( dm );
		GPU_normal_setup( dm );
		GPU_uv_setup( dm );
		if( col != NULL ) {
			/*if( realcol && dm->drawObject->colType == CD_TEXTURE_MCOL )  {
				col = 0;
			} else if( mcol && dm->drawObject->colType == CD_MCOL ) {
				col = 0;
			}
			
			if( col != 0 ) {*/
				unsigned char *colors = MEM_mallocN(dm->getNumTessFaces(dm)*4*3*sizeof(unsigned char), "cdDM_drawFacesTex_common");
				for( i=0; i < dm->getNumTessFaces(dm); i++ ) {
					for( j=0; j < 4; j++ ) {
						colors[i*12+j*3] = col[i*4+j].r;
						colors[i*12+j*3+1] = col[i*4+j].g;
						colors[i*12+j*3+2] = col[i*4+j].b;
					}
				}
				GPU_color3_upload(dm,colors);
				MEM_freeN(colors);
				if(realcol)
					dm->drawObject->colType = CD_TEXTURE_MCOL;
				else if(mcol)
					dm->drawObject->colType = CD_MCOL;
			//}
			GPU_color_setup( dm );
		}

		if( !GPU_buffer_legacy(dm) ) {
			/* warning!, this logic is incorrect, see bug [#27175]
			 * firstly, there are no checks for changes in context, such as texface image.
			 * secondly, drawParams() sets the GL context, so checking if there is a change
			 * from lastFlag is too late once glDrawArrays() runs, since drawing the arrays
			 * will use the modified, OpenGL settings.
			 * 
			 * However its tricky to fix this without duplicating the internal logic
			 * of drawParams(), perhaps we need an argument like...
			 * drawParams(..., keep_gl_state_but_return_when_changed) ?.
			 *
			 * We could also just disable VBO's here, since texface may be deprecated - campbell.
			 */
			
			glShadeModel( GL_SMOOTH );
			lastFlag = 0;
			for(i = 0; i < dm->drawObject->nelements/3; i++) {
				int actualFace = dm->drawObject->faceRemap[i];
				int flag = 1;

				if(drawParams) {
					flag = drawParams(tf? &tf[actualFace]: NULL, mcol!=NULL, mf[actualFace].mat_nr);
				}
				else {
					if(index) {
						orig = index[actualFace];
						if(orig == ORIGINDEX_NONE) continue;
						if(drawParamsMapped)
							flag = drawParamsMapped(userData, orig);
					}
					else
						if(drawParamsMapped)
							flag = drawParamsMapped(userData, actualFace);
				}
				if( flag != lastFlag ) {
					if( startFace < i ) {
						if( lastFlag != 0 ) { /* if the flag is 0 it means the face is hidden or invisible */
							if (lastFlag==1 && col)
								GPU_color_switch(1);
							else
								GPU_color_switch(0);
							glDrawArrays(GL_TRIANGLES,startFace*3,(i-startFace)*3);
						}
					}
					lastFlag = flag;
					startFace = i;
				}
			}
			if( startFace < dm->drawObject->nelements/3 ) {
				if( lastFlag != 0 ) { /* if the flag is 0 it means the face is hidden or invisible */
					if (lastFlag==1 && col)
						GPU_color_switch(1);
					else
						GPU_color_switch(0);
					glDrawArrays(GL_TRIANGLES,startFace*3,dm->drawObject->nelements-startFace*3);
				}
			}
		}

		GPU_buffer_unbind();
		glShadeModel( GL_FLAT );
	}
}

static void cdDM_drawFacesTex(DerivedMesh *dm, int (*setDrawOptions)(MTFace *tface, int has_vcol, int matnr))
{
	cdDM_drawFacesTex_common(dm, setDrawOptions, NULL, NULL);
}

static void cdDM_drawMappedFaces(DerivedMesh *dm, int (*setDrawOptions)(void *userData, int index, int *drawSmooth_r), void *userData, int useColors, int (*setMaterial)(int, void *attribs))
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mv = cddm->mvert;
	MFace *mf = cddm->mface;
	MCol *mc;
	float *nors= dm->getTessFaceDataArray(dm, CD_NORMAL);
	int i, orig, *index = DM_get_tessface_data_layer(dm, CD_ORIGINDEX);

	mc = DM_get_tessface_data_layer(dm, CD_ID_MCOL);
	if(!mc)
		mc = DM_get_tessface_data_layer(dm, CD_WEIGHT_MCOL);
	if(!mc)
		mc = DM_get_tessface_data_layer(dm, CD_MCOL);

	cdDM_update_normals_from_pbvh(dm);

	/* back-buffer always uses legacy since VBO's would need the
	 * color array temporarily overwritten for drawing, then reset. */
	if( GPU_buffer_legacy(dm) || G.f & G_BACKBUFSEL) {
		DEBUG_VBO( "Using legacy code. cdDM_drawMappedFaces\n" );
		for(i = 0; i < dm->numFaceData; i++, mf++) {
			int drawSmooth = (mf->flag & ME_SMOOTH);
			int draw= 1;

			orig= (index==NULL) ? i : *index++;
			
			if(orig == ORIGINDEX_NONE)
				draw= setMaterial(mf->mat_nr + 1, NULL);
			else if (setDrawOptions != NULL)
				draw= setDrawOptions(userData, orig, &drawSmooth);

			if(draw) {
				unsigned char *cp = NULL;

				if(useColors && mc)
					cp = (unsigned char *)&mc[i * 4];

				glShadeModel(drawSmooth?GL_SMOOTH:GL_FLAT);
				glBegin(mf->v4?GL_QUADS:GL_TRIANGLES);

				if (!drawSmooth) {
					if (nors) {
						glNormal3fv(nors);
					}
					else {
						float nor[3];
						if(mf->v4) {
							normal_quad_v3( nor,mv[mf->v1].co, mv[mf->v2].co, mv[mf->v3].co, mv[mf->v4].co);
						} else {
							normal_tri_v3( nor,mv[mf->v1].co, mv[mf->v2].co, mv[mf->v3].co);
						}
						glNormal3fv(nor);
					}

					if(cp) glColor3ub(cp[3], cp[2], cp[1]);
					glVertex3fv(mv[mf->v1].co);
					if(cp) glColor3ub(cp[7], cp[6], cp[5]);
					glVertex3fv(mv[mf->v2].co);
					if(cp) glColor3ub(cp[11], cp[10], cp[9]);
					glVertex3fv(mv[mf->v3].co);
					if(mf->v4) {
						if(cp) glColor3ub(cp[15], cp[14], cp[13]);
						glVertex3fv(mv[mf->v4].co);
					}
				} else {
					if(cp) glColor3ub(cp[3], cp[2], cp[1]);
					glNormal3sv(mv[mf->v1].no);
					glVertex3fv(mv[mf->v1].co);
					if(cp) glColor3ub(cp[7], cp[6], cp[5]);
					glNormal3sv(mv[mf->v2].no);
					glVertex3fv(mv[mf->v2].co);
					if(cp) glColor3ub(cp[11], cp[10], cp[9]);
					glNormal3sv(mv[mf->v3].no);
					glVertex3fv(mv[mf->v3].co);
					if(mf->v4) {
						if(cp) glColor3ub(cp[15], cp[14], cp[13]);
						glNormal3sv(mv[mf->v4].no);
						glVertex3fv(mv[mf->v4].co);
					}
				}

				glEnd();
			}
			
			if (nors) nors += 3;
		}
	}
	else { /* use OpenGL VBOs or Vertex Arrays instead for better, faster rendering */
		int prevstart = 0;
		GPU_vertex_setup(dm);
		GPU_normal_setup(dm);
		if( useColors && mc )
			GPU_color_setup(dm);
		if( !GPU_buffer_legacy(dm) ) {
			int tottri = dm->drawObject->nelements/3;
			glShadeModel(GL_SMOOTH);
			
			if(tottri == 0) {
				/* avoid buffer problems in following code */
			}
			if(setDrawOptions == NULL) {
				/* just draw the entire face array */
				glDrawArrays(GL_TRIANGLES, 0, (tottri-1) * 3);
			}
			else {
				/* we need to check if the next material changes */
				int next_actualFace= dm->drawObject->faceRemap[0];
				
				for( i = 0; i < tottri; i++ ) {
					//int actualFace = dm->drawObject->faceRemap[i];
					int actualFace = next_actualFace;
					MFace *mface= mf + actualFace;
					int drawSmooth= (mface->flag & ME_SMOOTH);
					int draw = 1;

					if(i != tottri-1)
						next_actualFace= dm->drawObject->faceRemap[i+1];

					orig= (index==NULL) ? actualFace : index[actualFace];

					if(orig == ORIGINDEX_NONE)
						draw= setMaterial(mface->mat_nr + 1, NULL);
					else if (setDrawOptions != NULL)
						draw= setDrawOptions(userData, orig, &drawSmooth);
	
					/* Goal is to draw as long of a contiguous triangle
					   array as possible, so draw when we hit either an
					   invisible triangle or at the end of the array */
					if(!draw || i == tottri - 1 || mf[actualFace].mat_nr != mf[next_actualFace].mat_nr) {
						if(prevstart != i)
							/* Add one to the length (via `draw')
							   if we're drawing at the end of the array */
							glDrawArrays(GL_TRIANGLES,prevstart*3, (i-prevstart+draw)*3);
						prevstart = i + 1;
					}
				}
			}

			glShadeModel(GL_FLAT);
		}
		GPU_buffer_unbind();
	}
}

static void cdDM_drawMappedFacesTex(DerivedMesh *dm, int (*setDrawOptions)(void *userData, int index), void *userData)
{
	cdDM_drawFacesTex_common(dm, NULL, setDrawOptions, userData);
}

static void cdDM_drawMappedFacesGLSL(DerivedMesh *dm, int (*setMaterial)(int, void *attribs), int (*setDrawOptions)(void *userData, int index), void *userData)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	GPUVertexAttribs gattribs;
	DMVertexAttribs attribs;
	MVert *mvert = cddm->mvert;
	MFace *mface = cddm->mface;
	MTFace *tf = dm->getTessFaceDataArray(dm, CD_MTFACE);
	float (*nors)[3] = dm->getTessFaceDataArray(dm, CD_NORMAL);
	int a, b, dodraw, matnr, new_matnr;
	int transp, new_transp, orig_transp;
	int orig, *index = dm->getTessFaceDataArray(dm, CD_ORIGINDEX);

	cdDM_update_normals_from_pbvh(dm);

	matnr = -1;
	dodraw = 0;
	transp = GPU_get_material_blend_mode();
	orig_transp = transp;

	glShadeModel(GL_SMOOTH);

	if( GPU_buffer_legacy(dm) || setDrawOptions != NULL ) {
		DEBUG_VBO( "Using legacy code. cdDM_drawMappedFacesGLSL\n" );
		memset(&attribs, 0, sizeof(attribs));

		glBegin(GL_QUADS);

		for(a = 0; a < dm->numFaceData; a++, mface++) {
			const int smoothnormal = (mface->flag & ME_SMOOTH);
			new_matnr = mface->mat_nr + 1;

			if(new_matnr != matnr) {
				glEnd();

				dodraw = setMaterial(matnr = new_matnr, &gattribs);
				if(dodraw)
					DM_vertex_attributes_from_gpu(dm, &gattribs, &attribs);

				glBegin(GL_QUADS);
			}

			if(!dodraw) {
				continue;
			}
			else if(setDrawOptions) {
				orig = (index)? index[a]: a;

				if(orig == ORIGINDEX_NONE) {
					/* since the material is set by setMaterial(), faces with no
					 * origin can be assumed to be generated by a modifier */ 
					
					/* continue */
				}
				else if(!setDrawOptions(userData, orig))
					continue;
			}

			if(tf) {
				new_transp = tf[a].transp;

				if(new_transp != transp) {
					glEnd();

					if(new_transp == GPU_BLEND_SOLID && orig_transp != GPU_BLEND_SOLID)
						GPU_set_material_blend_mode(orig_transp);
					else
						GPU_set_material_blend_mode(new_transp);
					transp = new_transp;

					glBegin(GL_QUADS);
				}
			}

			if(!smoothnormal) {
				if(nors) {
					glNormal3fv(nors[a]);
				}
				else {
					/* TODO ideally a normal layer should always be available */
					float nor[3];
					if(mface->v4) {
						normal_quad_v3( nor,mvert[mface->v1].co, mvert[mface->v2].co, mvert[mface->v3].co, mvert[mface->v4].co);
					} else {
						normal_tri_v3( nor,mvert[mface->v1].co, mvert[mface->v2].co, mvert[mface->v3].co);
					}
					glNormal3fv(nor);
				}
			}

#define PASSVERT(index, vert) {													\
		if(attribs.totorco)															\
			glVertexAttrib3fvARB(attribs.orco.glIndex, attribs.orco.array[index]);	\
		for(b = 0; b < attribs.tottface; b++) {										\
			MTFace *tf = &attribs.tface[b].array[a];								\
			glVertexAttrib2fvARB(attribs.tface[b].glIndex, tf->uv[vert]);			\
		}																			\
		for(b = 0; b < attribs.totmcol; b++) {										\
			MCol *cp = &attribs.mcol[b].array[a*4 + vert];							\
			GLubyte col[4];															\
			col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;				\
			glVertexAttrib4ubvARB(attribs.mcol[b].glIndex, col);					\
		}																			\
		if(attribs.tottang) {														\
			float *tang = attribs.tang.array[a*4 + vert];							\
			glVertexAttrib4fvARB(attribs.tang.glIndex, tang);						\
		}																			\
		if(smoothnormal)															\
			glNormal3sv(mvert[index].no);											\
		glVertex3fv(mvert[index].co);												\
	}

			PASSVERT(mface->v1, 0);
			PASSVERT(mface->v2, 1);
			PASSVERT(mface->v3, 2);
			if(mface->v4)
				PASSVERT(mface->v4, 3)
			else
				PASSVERT(mface->v3, 2)

		}
		glEnd();
	}
	else {
		GPUBuffer *buffer = NULL;
		char *varray = NULL;
		int numdata = 0, elementsize = 0, offset;
		int start = 0, numfaces = 0, prevdraw = 0, curface = 0;
		int i;

		MFace *mf = mface;
		GPUAttrib datatypes[GPU_MAX_ATTRIB]; /* TODO, messing up when switching materials many times - [#21056]*/
		memset(&attribs, 0, sizeof(attribs));

		GPU_vertex_setup(dm);
		GPU_normal_setup(dm);

		if( !GPU_buffer_legacy(dm) ) {
			for( i = 0; i < dm->drawObject->nelements/3; i++ ) {

				a = dm->drawObject->faceRemap[i];

				mface = mf + a;
				new_matnr = mface->mat_nr + 1;

				if(new_matnr != matnr ) {
					numfaces = curface - start;
					if( numfaces > 0 ) {

						if( dodraw ) {

							if( numdata != 0 ) {

								GPU_buffer_unlock(buffer);

								GPU_interleaved_attrib_setup(buffer,datatypes,numdata);
							}

							glDrawArrays(GL_TRIANGLES,start*3,numfaces*3);

							if( numdata != 0 ) {

								GPU_buffer_free(buffer, NULL);

								buffer = NULL;
							}

						}
					}
					numdata = 0;
					start = curface;
					prevdraw = dodraw;
					dodraw = setMaterial(matnr = new_matnr, &gattribs);
					if(dodraw) {
						DM_vertex_attributes_from_gpu(dm, &gattribs, &attribs);

						if(attribs.totorco) {
							datatypes[numdata].index = attribs.orco.glIndex;
							datatypes[numdata].size = 3;
							datatypes[numdata].type = GL_FLOAT;
							numdata++;
						}
						for(b = 0; b < attribs.tottface; b++) {
							datatypes[numdata].index = attribs.tface[b].glIndex;
							datatypes[numdata].size = 2;
							datatypes[numdata].type = GL_FLOAT;
							numdata++;
						}	
						for(b = 0; b < attribs.totmcol; b++) {
							datatypes[numdata].index = attribs.mcol[b].glIndex;
							datatypes[numdata].size = 4;
							datatypes[numdata].type = GL_UNSIGNED_BYTE;
							numdata++;
						}	
						if(attribs.tottang) {
							datatypes[numdata].index = attribs.tang.glIndex;
							datatypes[numdata].size = 4;
							datatypes[numdata].type = GL_FLOAT;
							numdata++;
						}
						if( numdata != 0 ) {
							elementsize = GPU_attrib_element_size( datatypes, numdata );
							buffer = GPU_buffer_alloc( elementsize*dm->drawObject->nelements, NULL );
							if( buffer == NULL ) {
								GPU_buffer_unbind();
								dm->drawObject->legacy = 1;
								return;
							}
							varray = GPU_buffer_lock_stream(buffer);
							if( varray == NULL ) {
								GPU_buffer_unbind();
								GPU_buffer_free(buffer, NULL);
								dm->drawObject->legacy = 1;
								return;
							}
						}
						else {
							/* if the buffer was set, dont use it again.
							 * prevdraw was assumed true but didnt run so set to false - [#21036] */
							prevdraw= 0;
							buffer= NULL;
						}
					}
				}
				if(!dodraw) {
					continue;
				}

				if(tf) {
					new_transp = tf[a].transp;

					if(new_transp != transp) {
						numfaces = curface - start;
						if( numfaces > 0 ) {
							if( dodraw ) {
								if( numdata != 0 ) {
									GPU_buffer_unlock(buffer);
									GPU_interleaved_attrib_setup(buffer,datatypes,numdata);
								}
								glDrawArrays(GL_TRIANGLES,start*3,(curface-start)*3);
								if( numdata != 0 ) {
									varray = GPU_buffer_lock_stream(buffer);
								}
							}
						}
						start = curface;

						if(new_transp == GPU_BLEND_SOLID && orig_transp != GPU_BLEND_SOLID)
							GPU_set_material_blend_mode(orig_transp);
						else
							GPU_set_material_blend_mode(new_transp);
						transp = new_transp;
					}
				}
				
				if( numdata != 0 ) {
					offset = 0;
					if(attribs.totorco) {
						VECCOPY((float *)&varray[elementsize*curface*3],(float *)attribs.orco.array[mface->v1]);
						VECCOPY((float *)&varray[elementsize*curface*3+elementsize],(float *)attribs.orco.array[mface->v2]);
						VECCOPY((float *)&varray[elementsize*curface*3+elementsize*2],(float *)attribs.orco.array[mface->v3]);
						offset += sizeof(float)*3;
					}
					for(b = 0; b < attribs.tottface; b++) {
						MTFace *tf = &attribs.tface[b].array[a];
						VECCOPY2D((float *)&varray[elementsize*curface*3+offset],tf->uv[0]);
						VECCOPY2D((float *)&varray[elementsize*curface*3+offset+elementsize],tf->uv[1]);

						VECCOPY2D((float *)&varray[elementsize*curface*3+offset+elementsize*2],tf->uv[2]);
						offset += sizeof(float)*2;
					}
					for(b = 0; b < attribs.totmcol; b++) {
						MCol *cp = &attribs.mcol[b].array[a*4 + 0];
						GLubyte col[4];
						col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
						QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset], col);
						cp = &attribs.mcol[b].array[a*4 + 1];
						col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
						QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset+elementsize], col);
						cp = &attribs.mcol[b].array[a*4 + 2];
						col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
						QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset+elementsize*2], col);
						offset += sizeof(unsigned char)*4;
					}	
					if(attribs.tottang) {
						float *tang = attribs.tang.array[a*4 + 0];
						QUATCOPY((float *)&varray[elementsize*curface*3+offset], tang);
						tang = attribs.tang.array[a*4 + 1];
						QUATCOPY((float *)&varray[elementsize*curface*3+offset+elementsize], tang);
						tang = attribs.tang.array[a*4 + 2];
						QUATCOPY((float *)&varray[elementsize*curface*3+offset+elementsize*2], tang);
						offset += sizeof(float)*4;
					}
				}
				curface++;
				if(mface->v4) {
					if( numdata != 0 ) {
						offset = 0;
						if(attribs.totorco) {
							VECCOPY((float *)&varray[elementsize*curface*3],(float *)attribs.orco.array[mface->v3]);
							VECCOPY((float *)&varray[elementsize*curface*3+elementsize],(float *)attribs.orco.array[mface->v4]);
							VECCOPY((float *)&varray[elementsize*curface*3+elementsize*2],(float *)attribs.orco.array[mface->v1]);
							offset += sizeof(float)*3;
						}
						for(b = 0; b < attribs.tottface; b++) {
							MTFace *tf = &attribs.tface[b].array[a];
							VECCOPY2D((float *)&varray[elementsize*curface*3+offset],tf->uv[2]);
							VECCOPY2D((float *)&varray[elementsize*curface*3+offset+elementsize],tf->uv[3]);
							VECCOPY2D((float *)&varray[elementsize*curface*3+offset+elementsize*2],tf->uv[0]);
							offset += sizeof(float)*2;
						}
						for(b = 0; b < attribs.totmcol; b++) {
							MCol *cp = &attribs.mcol[b].array[a*4 + 2];
							GLubyte col[4];
							col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
							QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset], col);
							cp = &attribs.mcol[b].array[a*4 + 3];
							col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
							QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset+elementsize], col);
							cp = &attribs.mcol[b].array[a*4 + 0];
							col[0]= cp->b; col[1]= cp->g; col[2]= cp->r; col[3]= cp->a;
							QUATCOPY((unsigned char *)&varray[elementsize*curface*3+offset+elementsize*2], col);
							offset += sizeof(unsigned char)*4;
						}	
						if(attribs.tottang) {
							float *tang = attribs.tang.array[a*4 + 2];
							QUATCOPY((float *)&varray[elementsize*curface*3+offset], tang);
							tang = attribs.tang.array[a*4 + 3];
							QUATCOPY((float *)&varray[elementsize*curface*3+offset+elementsize], tang);
							tang = attribs.tang.array[a*4 + 0];
							QUATCOPY((float *)&varray[elementsize*curface*3+offset+elementsize*2], tang);
							offset += sizeof(float)*4;
						}
					}
					curface++;
					i++;
				}
			}
			numfaces = curface - start;
			if( numfaces > 0 ) {
				if( dodraw ) {
					if( numdata != 0 ) {
						GPU_buffer_unlock(buffer);
						GPU_interleaved_attrib_setup(buffer,datatypes,numdata);
					}
					glDrawArrays(GL_TRIANGLES,start*3,(curface-start)*3);
				}
			}
			GPU_buffer_unbind();
		}
		GPU_buffer_free( buffer, NULL );
	}

	glShadeModel(GL_FLAT);
}

static void cdDM_drawFacesGLSL(DerivedMesh *dm, int (*setMaterial)(int, void *attribs))
{
	dm->drawMappedFacesGLSL(dm, setMaterial, NULL, NULL);
}

static void cdDM_drawMappedEdges(DerivedMesh *dm, int (*setDrawOptions)(void *userData, int index), void *userData)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *vert = cddm->mvert;
	MEdge *edge = cddm->medge;
	int i, orig, *index = DM_get_edge_data_layer(dm, CD_ORIGINDEX);

	glBegin(GL_LINES);
	for(i = 0; i < dm->numEdgeData; i++, edge++) {
		if(index) {
			orig = *index++;
			if(setDrawOptions && orig == ORIGINDEX_NONE) continue;
		}
		else
			orig = i;

		if(!setDrawOptions || setDrawOptions(userData, orig)) {
			glVertex3fv(vert[edge->v1].co);
			glVertex3fv(vert[edge->v2].co);
		}
	}
	glEnd();
}

static void cdDM_foreachMappedVert(
						   DerivedMesh *dm,
						   void (*func)(void *userData, int index, float *co,
										float *no_f, short *no_s),
						   void *userData)
{
	MVert *mv = CDDM_get_verts(dm);
	int i, orig, *index = DM_get_vert_data_layer(dm, CD_ORIGINDEX);

	for(i = 0; i < dm->numVertData; i++, mv++) {
		if(index) {
			orig = *index++;
			if(orig == ORIGINDEX_NONE) continue;
			func(userData, orig, mv->co, NULL, mv->no);
		}
		else
			func(userData, i, mv->co, NULL, mv->no);
	}
}

static void cdDM_foreachMappedEdge(
						   DerivedMesh *dm,
						   void (*func)(void *userData, int index,
										float *v0co, float *v1co),
						   void *userData)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) dm;
	MVert *mv = cddm->mvert;
	MEdge *med = cddm->medge;
	int i, orig, *index = DM_get_edge_data_layer(dm, CD_ORIGINDEX);

	for(i = 0; i < dm->numEdgeData; i++, med++) {
		if (index) {
			orig = *index++;
			if(orig == ORIGINDEX_NONE) continue;
			func(userData, orig, mv[med->v1].co, mv[med->v2].co);
		}
		else
			func(userData, i, mv[med->v1].co, mv[med->v2].co);
	}
}

static void cdDM_foreachMappedFaceCenter(
						   DerivedMesh *dm,
						   void (*func)(void *userData, int index,
										float *cent, float *no),
						   void *userData)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	MVert *mv = cddm->mvert;
	MPoly *mf = cddm->mpoly;
	MLoop *ml = cddm->mloop;
	int i, j, orig, *index;

	index = CustomData_get_layer(&dm->polyData, CD_ORIGINDEX);
	mf = cddm->mpoly;
	for(i = 0; i < dm->numPolyData; i++, mf++) {
		float cent[3];
		float no[3];

		if (index) {
			orig = *index++;
			if(orig == ORIGINDEX_NONE) continue;
		} else
			orig = i;
		
		ml = &cddm->mloop[mf->loopstart];
		cent[0] = cent[1] = cent[2] = 0.0f;
		for (j=0; j<mf->totloop; j++, ml++) {
			add_v3_v3v3(cent, cent, mv[ml->v].co);
		}
		mul_v3_fl(cent, 1.0f / (float)j);

		ml = &cddm->mloop[mf->loopstart];
		if (j > 3) {
			normal_quad_v3(no, mv[ml->v].co, mv[(ml+1)->v].co,
				       mv[(ml+2)->v].co, mv[(ml+3)->v].co);
		} else {
			normal_tri_v3(no, mv[ml->v].co, mv[(ml+1)->v].co,
				       mv[(ml+2)->v].co);
		}

		func(userData, orig, cent, no);
	}

}

static void cdDM_recalcTesselation(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;

	dm->numFaceData = mesh_recalcTesselation(&dm->faceData, &dm->loopData, 
		&dm->polyData, cddm->mvert, dm->numFaceData, dm->numLoopData, 
		dm->numPolyData, 1, 0);
	
	cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);
}

/*ignores original poly origindex layer*/
static void cdDM_recalcTesselation2(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;

	dm->numFaceData = mesh_recalcTesselation(&dm->faceData, &dm->loopData, 
		&dm->polyData, cddm->mvert, dm->numFaceData, dm->numLoopData, 
		dm->numPolyData, 0, 0);
	
	cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);
}

void CDDM_recalc_tesselation(DerivedMesh *dm, int orig_use_polyorig)
{
	if (orig_use_polyorig)
		cdDM_recalcTesselation(dm);
	else
		cdDM_recalcTesselation2(dm);
}

static void cdDM_free_internal(CDDerivedMesh *cddm)
{
	if(cddm->fmap) MEM_freeN(cddm->fmap);
	if(cddm->fmap_mem) MEM_freeN(cddm->fmap_mem);
}

static void cdDM_release(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;

	if (DM_release(dm)) {
		cdDM_free_internal(cddm);
		MEM_freeN(cddm);
	}
}

int CDDM_Check(DerivedMesh *dm)
{
	return dm && dm->getMinMax == cdDM_getMinMax;
}

/**************** CDDM interface functions ****************/
static CDDerivedMesh *cdDM_create(const char *desc)
{
	CDDerivedMesh *cddm;
	DerivedMesh *dm;

	cddm = MEM_callocN(sizeof(*cddm), desc);
	dm = &cddm->dm;

	dm->getMinMax = cdDM_getMinMax;

	dm->getNumVerts = cdDM_getNumVerts;
	dm->getNumEdges = cdDM_getNumEdges;
	dm->getNumTessFaces = cdDM_getNumTessFaces;
	dm->getNumFaces = cdDM_getNumFaces;

	dm->newFaceIter = cdDM_newFaceIter;

	dm->getVert = cdDM_getVert;
	dm->getEdge = cdDM_getEdge;
	dm->getTessFace = cdDM_getFace;
	dm->copyVertArray = cdDM_copyVertArray;
	dm->copyEdgeArray = cdDM_copyEdgeArray;
	dm->copyTessFaceArray = cdDM_copyFaceArray;
	dm->getVertData = DM_get_vert_data;
	dm->getEdgeData = DM_get_edge_data;
	dm->getTessFaceData = DM_get_face_data;
	dm->getVertDataArray = DM_get_vert_data_layer;
	dm->getEdgeDataArray = DM_get_edge_data_layer;
	dm->getTessFaceDataArray = DM_get_tessface_data_layer;
	
	//doesn't work yet for all cases
	//dm->recalcTesselation = cdDM_recalcTesselation;

	dm->getVertCos = cdDM_getVertCos;
	dm->getVertCo = cdDM_getVertCo;
	dm->getVertNo = cdDM_getVertNo;

	dm->getPBVH = cdDM_getPBVH;
	dm->getFaceMap = cdDM_getFaceMap;

	dm->drawVerts = cdDM_drawVerts;

	dm->drawUVEdges = cdDM_drawUVEdges;
	dm->drawEdges = cdDM_drawEdges;
	dm->drawLooseEdges = cdDM_drawLooseEdges;
	dm->drawMappedEdges = cdDM_drawMappedEdges;

	dm->drawFacesSolid = cdDM_drawFacesSolid;
	dm->drawFacesColored = cdDM_drawFacesColored;
	dm->drawFacesTex = cdDM_drawFacesTex;
	dm->drawFacesGLSL = cdDM_drawFacesGLSL;
	dm->drawMappedFaces = cdDM_drawMappedFaces;
	dm->drawMappedFacesTex = cdDM_drawMappedFacesTex;
	dm->drawMappedFacesGLSL = cdDM_drawMappedFacesGLSL;

	dm->foreachMappedVert = cdDM_foreachMappedVert;
	dm->foreachMappedEdge = cdDM_foreachMappedEdge;
	dm->foreachMappedFaceCenter = cdDM_foreachMappedFaceCenter;

	dm->release = cdDM_release;

	return cddm;
}

DerivedMesh *CDDM_new(int numVerts, int numEdges, int numFaces, int numLoops, int numPolys)
{
	CDDerivedMesh *cddm = cdDM_create("CDDM_new dm");
	DerivedMesh *dm = &cddm->dm;

	DM_init(dm, DM_TYPE_CDDM, numVerts, numEdges, numFaces, numLoops, numPolys);

	CustomData_add_layer(&dm->vertData, CD_ORIGINDEX, CD_CALLOC, NULL, numVerts);
	CustomData_add_layer(&dm->edgeData, CD_ORIGINDEX, CD_CALLOC, NULL, numEdges);
	CustomData_add_layer(&dm->faceData, CD_ORIGINDEX, CD_CALLOC, NULL, numFaces);
	CustomData_add_layer(&dm->polyData, CD_ORIGINDEX, CD_CALLOC, NULL, numPolys);

	CustomData_add_layer(&dm->vertData, CD_MVERT, CD_CALLOC, NULL, numVerts);
	CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_CALLOC, NULL, numEdges);
	CustomData_add_layer(&dm->faceData, CD_MFACE, CD_CALLOC, NULL, numFaces);
	CustomData_add_layer(&dm->loopData, CD_MLOOP, CD_CALLOC, NULL, numLoops);
	CustomData_add_layer(&dm->polyData, CD_MPOLY, CD_CALLOC, NULL, numPolys);

	cddm->mvert = CustomData_get_layer(&dm->vertData, CD_MVERT);
	cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);
	cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);
	cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
	cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);

	return dm;
}

DerivedMesh *CDDM_from_mesh(Mesh *mesh, Object *UNUSED(ob))
{
	CDDerivedMesh *cddm = cdDM_create("CDDM_from_mesh dm");
	DerivedMesh *dm = &cddm->dm;
	CustomDataMask mask = CD_MASK_MESH & (~CD_MASK_MDISPS);
	int alloctype;

	/* this does a referenced copy, with an exception for fluidsim */

	DM_init(dm, DM_TYPE_CDDM, mesh->totvert, mesh->totedge, mesh->totface,
	            mesh->totloop, mesh->totpoly);

	dm->deformedOnly = 1;

	alloctype= CD_REFERENCE;

	CustomData_merge(&mesh->vdata, &dm->vertData, mask, alloctype,
					 mesh->totvert);
	CustomData_merge(&mesh->edata, &dm->edgeData, mask, alloctype,
					 mesh->totedge);
	CustomData_merge(&mesh->fdata, &dm->faceData, mask|CD_MASK_ORIGINDEX, alloctype,
					 mesh->totface);
	CustomData_merge(&mesh->ldata, &dm->loopData, mask, alloctype,
	                 mesh->totloop);
	CustomData_merge(&mesh->pdata, &dm->polyData, mask, alloctype,
	                 mesh->totpoly);

	cddm->mvert = CustomData_get_layer(&dm->vertData, CD_MVERT);
	cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);
	cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
	cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);
	cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);

	if (!CustomData_has_layer(&cddm->dm.faceData, CD_ORIGINDEX))
		CustomData_add_layer(&dm->faceData, CD_ORIGINDEX, CD_CALLOC, NULL, mesh->totface);

	return dm;
}

DerivedMesh *disabled__CDDM_from_editmesh(EditMesh *em, Mesh *UNUSED(me))
{
	DerivedMesh *dm = CDDM_new(BLI_countlist(&em->verts),
	                           BLI_countlist(&em->edges),
	                           BLI_countlist(&em->faces), 0, 0);
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	EditVert *eve;
	EditEdge *eed;
	EditFace *efa;
	MVert *mvert = cddm->mvert;
	MEdge *medge = cddm->medge;
	MFace *mface = cddm->mface;
	int i, *index;

	dm->deformedOnly = 1;

	CustomData_merge(&em->vdata, &dm->vertData, CD_MASK_DERIVEDMESH,
					 CD_CALLOC, dm->numVertData);
	/* CustomData_merge(&em->edata, &dm->edgeData, CD_MASK_DERIVEDMESH,
					 CD_CALLOC, dm->numEdgeData); */
	CustomData_merge(&em->fdata, &dm->faceData, CD_MASK_DERIVEDMESH,
					 CD_CALLOC, dm->numFaceData);
	CustomData_merge(&em->fdata, &dm->faceData, CD_MASK_DERIVEDMESH,
	                 CD_CALLOC, dm->numFaceData);

	/* set eve->hash to vert index */
	for(i = 0, eve = em->verts.first; eve; eve = eve->next, ++i)
		eve->tmp.l = i;

	/* Need to be able to mark loose edges */
	for(eed = em->edges.first; eed; eed = eed->next) {
		eed->f2 = 0;
	}
	for(efa = em->faces.first; efa; efa = efa->next) {
		efa->e1->f2 = 1;
		efa->e2->f2 = 1;
		efa->e3->f2 = 1;
		if(efa->e4) efa->e4->f2 = 1;
	}

	index = dm->getVertDataArray(dm, CD_ORIGINDEX);
	for(i = 0, eve = em->verts.first; i < dm->numVertData;
		i++, eve = eve->next, index++) {
		MVert *mv = &mvert[i];

		VECCOPY(mv->co, eve->co);

		normal_float_to_short_v3(mv->no, eve->no);
		mv->bweight = (unsigned char) (eve->bweight * 255.0f);

		mv->flag = 0;

		*index = i;

		CustomData_from_em_block(&em->vdata, &dm->vertData, eve->data, i);
	}

	index = dm->getEdgeDataArray(dm, CD_ORIGINDEX);
	for(i = 0, eed = em->edges.first; i < dm->numEdgeData;
		i++, eed = eed->next, index++) {
		MEdge *med = &medge[i];

		med->v1 = eed->v1->tmp.l;
		med->v2 = eed->v2->tmp.l;
		med->crease = (unsigned char) (eed->crease * 255.0f);
		med->bweight = (unsigned char) (eed->bweight * 255.0f);
		med->flag = ME_EDGEDRAW|ME_EDGERENDER;
		
		if(eed->seam) med->flag |= ME_SEAM;
		if(eed->sharp) med->flag |= ME_SHARP;
		if(!eed->f2) med->flag |= ME_LOOSEEDGE;

		*index = i;

		/* CustomData_from_em_block(&em->edata, &dm->edgeData, eed->data, i); */
	}

	index = dm->getTessFaceDataArray(dm, CD_ORIGINDEX);
	for(i = 0, efa = em->faces.first; i < dm->numFaceData;
		i++, efa = efa->next, index++) {
		MFace *mf = &mface[i];

		mf->v1 = efa->v1->tmp.l;
		mf->v2 = efa->v2->tmp.l;
		mf->v3 = efa->v3->tmp.l;
		mf->v4 = efa->v4 ? efa->v4->tmp.l : 0;
		mf->mat_nr = efa->mat_nr;
		mf->flag = efa->flag;

		*index = i;

		CustomData_from_em_block(&em->fdata, &dm->faceData, efa->data, i);
		test_index_face(mf, &dm->faceData, i, efa->v4?4:3);
	}

	return dm;
}

DerivedMesh *CDDM_from_curve(Object *ob)
{
	return CDDM_from_curve_customDB(ob, &ob->disp);
}

DerivedMesh *CDDM_from_curve_customDB(Object *ob, ListBase *dispbase)
{
	DerivedMesh *dm;
	CDDerivedMesh *cddm;
	MVert *allvert;
	MEdge *alledge;
	MFace *allface;
	MLoop *allloop;
	MPoly *allpoly;
	int totvert, totedge, totface, totloop, totpoly;

	if (nurbs_to_mdata_customdb(ob, dispbase, &allvert, &totvert, &alledge,
		&totedge, &allface, &allloop, &allpoly, &totface, &totloop, &totpoly) != 0) {
		/* Error initializing mdata. This often happens when curve is empty */
		return CDDM_new(0, 0, 0, 0, 0);
	}

	dm = CDDM_new(totvert, totedge, totface, totloop, totpoly);
	dm->deformedOnly = 1;

	cddm = (CDDerivedMesh*)dm;

	memcpy(cddm->mvert, allvert, totvert*sizeof(MVert));
	memcpy(cddm->medge, alledge, totedge*sizeof(MEdge));
	memcpy(cddm->mface, allface, totface*sizeof(MFace));
	memcpy(cddm->mloop, allloop, totloop*sizeof(MLoop));
	memcpy(cddm->mpoly, allpoly, totpoly*sizeof(MPoly));

	MEM_freeN(allvert);
	MEM_freeN(alledge);
	MEM_freeN(allface);
	MEM_freeN(allloop);
	MEM_freeN(allpoly);

	return dm;
}

static void loops_to_customdata_corners(BMesh *bm, CustomData *facedata,
					  int cdindex, BMLoop *l3[3],
					  int numCol, int numTex)
{
	BMLoop *l;
	BMFace *f = l3[0]->f;
	MTFace *texface;
	MTexPoly *texpoly;
	MCol *mcol;
	MLoopCol *mloopcol;
	MLoopUV *mloopuv;
	int i, j, hasWCol = CustomData_has_layer(&bm->ldata, CD_WEIGHT_MLOOPCOL);

	for(i=0; i < numTex; i++){
		texface = CustomData_get_n(facedata, CD_MTFACE, cdindex, i);
		texpoly = CustomData_bmesh_get_n(&bm->pdata, f->head.data, CD_MTEXPOLY, i);
		
		texface->tpage = texpoly->tpage;
		texface->flag = texpoly->flag;
		texface->transp = texpoly->transp;
		texface->mode = texpoly->mode;
		texface->tile = texpoly->tile;
		texface->unwrap = texpoly->unwrap;
	
		for (j=0; j<3; j++) {
			l = l3[j];
			mloopuv = CustomData_bmesh_get_n(&bm->ldata, l->head.data, CD_MLOOPUV, i);
			texface->uv[j][0] = mloopuv->uv[0];
			texface->uv[j][1] = mloopuv->uv[1];
		}
	}

	for(i=0; i < numCol; i++){
		mcol = CustomData_get_n(facedata, CD_MCOL, cdindex, i);
		
		for (j=0; j<3; j++) {
			l = l3[j];
			mloopcol = CustomData_bmesh_get_n(&bm->ldata, l->head.data, CD_MLOOPCOL, i);
			mcol[j].r = mloopcol->r;
			mcol[j].g = mloopcol->g;
			mcol[j].b = mloopcol->b;
			mcol[j].a = mloopcol->a;
		}
	}

	if (hasWCol) {
		mcol = CustomData_get(facedata, cdindex, CD_WEIGHT_MCOL);

		for (j=0; j<3; j++) {
			l = l3[j];
			mloopcol = CustomData_bmesh_get(&bm->ldata, l->head.data, CD_WEIGHT_MLOOPCOL);
			mcol[j].r = mloopcol->r;
			mcol[j].g = mloopcol->g;
			mcol[j].b = mloopcol->b;
			mcol[j].a = mloopcol->a;
		}
	}
}

DerivedMesh *CDDM_from_BMEditMesh(BMEditMesh *em, Mesh *me, int use_mdisps)
{
	DerivedMesh *dm = CDDM_new(em->bm->totvert, em->bm->totedge, 
	                       em->tottri, em->bm->totloop, em->bm->totface);
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	BMesh *bm = em->bm;
	BMIter iter, liter;
	BMVert *eve;
	BMEdge *eed;
	BMFace *efa;
	MVert *mvert = cddm->mvert;
	MEdge *medge = cddm->medge;
	MFace *mface = cddm->mface;
	MLoop *mloop = cddm->mloop;
	MPoly *mpoly = cddm->mpoly;
	int numCol = CustomData_number_of_layers(&em->bm->ldata, CD_MLOOPCOL);
	int numTex = CustomData_number_of_layers(&em->bm->pdata, CD_MTEXPOLY);
	int i, j, *index, add_orig;
	int has_crease, has_edge_bweight, has_vert_bweight;
	int flag;
	
	has_edge_bweight = CustomData_has_layer(&em->bm->edata, CD_BWEIGHT);
	has_vert_bweight = CustomData_has_layer(&em->bm->vdata, CD_BWEIGHT);
	has_crease = CustomData_has_layer(&em->bm->edata, CD_CREASE);
	
	dm->deformedOnly = 1;
	
	/*don't add origindex layer if one already exists*/
	add_orig = !CustomData_has_layer(&em->bm->pdata, CD_ORIGINDEX);

	flag = use_mdisps ? CD_MASK_DERIVEDMESH|CD_MASK_MDISPS : CD_MASK_DERIVEDMESH;
	
	/*don't process shapekeys, we only feed them through the modifier stack as needed,
      e.g. for applying modifiers or the like*/
	flag &= ~CD_SHAPEKEY;
	CustomData_merge(&em->bm->vdata, &dm->vertData, flag,
	                 CD_CALLOC, dm->numVertData);
	CustomData_merge(&em->bm->edata, &dm->edgeData, flag,
	                 CD_CALLOC, dm->numEdgeData);
	CustomData_merge(&em->bm->ldata, &dm->loopData, flag,
	                 CD_CALLOC, dm->numLoopData);
	CustomData_merge(&em->bm->pdata, &dm->polyData, flag,
	                 CD_CALLOC, dm->numPolyData);
	
	/*add tesselation mface layers*/
	CustomData_from_bmeshpoly(&dm->faceData, &dm->polyData, &dm->loopData, em->tottri);

	/* set vert index */
	eve = BMIter_New(&iter, bm, BM_VERTS_OF_MESH, NULL);
	for (i=0; eve; eve=BMIter_Step(&iter), i++)
		BMINDEX_SET(eve, i);

	index = dm->getVertDataArray(dm, CD_ORIGINDEX);

	eve = BMIter_New(&iter, bm, BM_VERTS_OF_MESH, NULL);
	for (i=0; eve; eve=BMIter_Step(&iter), i++, index++) {
		MVert *mv = &mvert[i];

		VECCOPY(mv->co, eve->co);

		BMINDEX_SET(eve, i);

		mv->no[0] = eve->no[0] * 32767.0;
		mv->no[1] = eve->no[1] * 32767.0;
		mv->no[2] = eve->no[2] * 32767.0;

		mv->flag = BMFlags_To_MEFlags(eve);

		if (has_vert_bweight)
			mv->bweight = (unsigned char)(BM_GetCDf(&bm->vdata, eve, CD_BWEIGHT)*255.0f);

		if (add_orig) *index = i;

		CustomData_from_bmesh_block(&bm->vdata, &dm->vertData, eve->head.data, i);
	}

	index = dm->getEdgeDataArray(dm, CD_ORIGINDEX);
	eed = BMIter_New(&iter, bm, BM_EDGES_OF_MESH, NULL);
	for (i=0; eed; eed=BMIter_Step(&iter), i++, index++) {
		MEdge *med = &medge[i];

		BMINDEX_SET(eed, i);

		med->v1 = BMINDEX_GET(eed->v1);
		med->v2 = BMINDEX_GET(eed->v2);
		med->flag = ME_EDGEDRAW|ME_EDGERENDER;

		if (has_crease)
			med->crease = (unsigned char)(BM_GetCDf(&bm->edata, eed, CD_CREASE)*255.0f);
		if (has_edge_bweight)
			med->bweight = (unsigned char)(BM_GetCDf(&bm->edata, eed, CD_BWEIGHT)*255.0f);
		
		med->flag = BMFlags_To_MEFlags(eed);

		CustomData_from_bmesh_block(&bm->edata, &dm->edgeData, eed->head.data, i);
		if (add_orig) *index = i;
	}

	efa = BMIter_New(&iter, bm, BM_FACES_OF_MESH, NULL);
	for (i=0; efa; i++, efa=BMIter_Step(&iter)) {
		BMINDEX_SET(efa, i);
	}

	index = dm->getTessFaceDataArray(dm, CD_ORIGINDEX);
	for(i = 0; i < dm->numFaceData; i++, index++) {
		MFace *mf = &mface[i];
		BMLoop **l = em->looptris[i];
		efa = l[0]->f;

		mf->v1 = BMINDEX_GET(l[0]->v);
		mf->v2 = BMINDEX_GET(l[1]->v);
		mf->v3 = BMINDEX_GET(l[2]->v);
		mf->v4 = 0;
		mf->mat_nr = efa->mat_nr;
		mf->flag = BMFlags_To_MEFlags(efa);
		
		*index = add_orig ? BMINDEX_GET(efa) : *(int*)CustomData_bmesh_get(&bm->pdata, efa->head.data, CD_ORIGINDEX);

		loops_to_customdata_corners(bm, &dm->faceData, i, l, numCol, numTex);
		test_index_face(mf, &dm->faceData, i, 3);
	}
	
	index = CustomData_get_layer(&dm->polyData, CD_ORIGINDEX);
	j = 0;
	efa = BMIter_New(&iter, bm, BM_FACES_OF_MESH, NULL);
	for (i=0; efa; i++, efa=BMIter_Step(&iter), index++) {
		BMLoop *l;
		MPoly *mp = &mpoly[i];

		mp->totloop = efa->len;
		mp->flag = BMFlags_To_MEFlags(efa);
		mp->loopstart = j;
		mp->mat_nr = efa->mat_nr;
		
		BM_ITER(l, &liter, bm, BM_LOOPS_OF_FACE, efa) {
			mloop->v = BMINDEX_GET(l->v);
			mloop->e = BMINDEX_GET(l->e);
			CustomData_from_bmesh_block(&bm->ldata, &dm->loopData, l->head.data, j);

			j++;
			mloop++;
		}

		CustomData_from_bmesh_block(&bm->pdata, &dm->polyData, efa->head.data, i);

		if (add_orig) *index = i;
	}

	return dm;
}

typedef struct CDDM_LoopIter {
	DMLoopIter head;
	CDDerivedMesh *cddm;
	int len, i;
} CDDM_LoopIter;

typedef struct CDDM_FaceIter {
	DMFaceIter head;
	CDDerivedMesh *cddm;
	CDDM_LoopIter liter;
} CDDM_FaceIter;

void cddm_freeiter(void *self)
{
	MEM_freeN(self);
}

void cddm_stepiter(void *self)
{
	CDDM_FaceIter *iter = self;
	MPoly *mp;
	
	mp = iter->cddm->mpoly + iter->head.index;
	mp->flag = iter->head.flags;
	mp->mat_nr = iter->head.mat_nr;

	iter->head.index++;
	if (iter->head.index >= iter->cddm->dm.numPolyData) {
		iter->head.done = 1;
		return;
	}

	mp = iter->cddm->mpoly + iter->head.index;

	iter->head.flags = mp->flag;
	iter->head.mat_nr = mp->mat_nr;
	iter->head.len = mp->totloop;
}

void *cddm_faceiter_getcddata(void *self, int type, int layer)
{
	CDDM_FaceIter *iter = self;

	if (layer == -1) return CustomData_get(&iter->cddm->dm.polyData, 
		                               iter->head.index, type);
	else return CustomData_get_n(&iter->cddm->dm.polyData, type, 
		                    iter->head.index, layer);
}

void *cddm_loopiter_getcddata(void *self, int type, int layer)
{
	CDDM_LoopIter *iter = self;

	if (layer == -1) return CustomData_get(&iter->cddm->dm.loopData, 
		                               iter->head.index, type);
	else return CustomData_get_n(&iter->cddm->dm.loopData, type, 
	                             iter->head.index, layer);
}

void *cddm_loopiter_getvertcddata(void *self, int type, int layer)
{
	CDDM_LoopIter *iter = self;

	if (layer == -1) return CustomData_get(&iter->cddm->dm.vertData, 
		                               iter->cddm->mloop[iter->head.index].v,
					       type);
	else return CustomData_get_n(&iter->cddm->dm.vertData, type, 
	                             iter->cddm->mloop[iter->head.index].v, layer);
}

DMLoopIter *cddmiter_get_loopiter(void *self)
{
	CDDM_FaceIter *iter = self;
	CDDM_LoopIter *liter = &iter->liter;
	MPoly *mp = iter->cddm->mpoly + iter->head.index;

	liter->i = -1;
	liter->len = iter->head.len;
	liter->head.index = mp->loopstart-1;
	liter->head.done = 0;

	liter->head.step(liter);

	return (DMLoopIter*) liter;
}

void cddm_loopiter_step(void *self)
{
	CDDM_LoopIter *liter = self;
	MLoop *ml;

	liter->i++;
	liter->head.index++;

	if (liter->i == liter->len) {
		liter->head.done = 1;
		return;
	}

	ml = liter->cddm->mloop + liter->head.index;

	liter->head.eindex = ml->e;
	liter->head.v = liter->cddm->mvert[ml->v];
	liter->head.vindex = ml->v;
}

DMFaceIter *cdDM_newFaceIter(DerivedMesh *source)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*) source;
	CDDM_FaceIter *iter = MEM_callocN(sizeof(CDDM_FaceIter), "DMFaceIter from cddm");

	iter->head.free = cddm_freeiter;
	iter->head.step = cddm_stepiter;
	iter->head.getCDData = cddm_faceiter_getcddata;
	iter->head.getLoopsIter = cddmiter_get_loopiter;

	iter->liter.head.step = cddm_loopiter_step;
	iter->liter.head.getLoopCDData = cddm_loopiter_getcddata;
	iter->liter.head.getVertCDData = cddm_loopiter_getvertcddata;
	iter->liter.cddm = cddm;

	iter->cddm = cddm;

	if (source->numFaceData) {
		iter->head.index = -1;
		iter->head.step(iter);
	} else {
		iter->head.done = 1;
	}

	return (DMFaceIter*) iter;
}

DerivedMesh *CDDM_copy(DerivedMesh *source, int faces_from_tessfaces)
{
	CDDerivedMesh *cddm = cdDM_create("CDDM_copy cddm");
	DerivedMesh *dm = &cddm->dm;
	int numVerts = source->numVertData;
	int numEdges = source->numEdgeData;
	int numFaces = source->numFaceData;
	int numLoops = source->numLoopData;
	int numPolys = source->numPolyData;

	/* ensure these are created if they are made on demand */
	source->getVertDataArray(source, CD_ORIGINDEX);
	source->getEdgeDataArray(source, CD_ORIGINDEX);
	source->getTessFaceDataArray(source, CD_ORIGINDEX);

	/* this initializes dm, and copies all non mvert/medge/mface layers */
	DM_from_template(dm, source, DM_TYPE_CDDM, numVerts, numEdges, numFaces,
		numLoops, numPolys);
	dm->deformedOnly = source->deformedOnly;

	CustomData_copy_data(&source->vertData, &dm->vertData, 0, 0, numVerts);
	CustomData_copy_data(&source->edgeData, &dm->edgeData, 0, 0, numEdges);
	CustomData_copy_data(&source->faceData, &dm->faceData, 0, 0, numFaces);

	/* now add mvert/medge/mface layers */
	cddm->mvert = source->dupVertArray(source);
	cddm->medge = source->dupEdgeArray(source);
	cddm->mface = source->dupTessFaceArray(source);

	CustomData_add_layer(&dm->vertData, CD_MVERT, CD_ASSIGN, cddm->mvert, numVerts);
	CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_ASSIGN, cddm->medge, numEdges);
	CustomData_add_layer(&dm->faceData, CD_MFACE, CD_ASSIGN, cddm->mface, numFaces);
	
	if (!faces_from_tessfaces)
		DM_DupPolys(source, dm);
 	else
		CDDM_tessfaces_to_faces(dm);

	cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
	cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);

	return dm;
}

/* note, the CD_ORIGINDEX layers are all 0, so if there is a direct
 * relationship betwen mesh data this needs to be set by the caller. */
DerivedMesh *CDDM_from_template(DerivedMesh *source,
                                int numVerts, int numEdges, int numFaces,
								int numLoops, int numPolys)
{
	CDDerivedMesh *cddm = cdDM_create("CDDM_from_template dest");
	DerivedMesh *dm = &cddm->dm;

	/* ensure these are created if they are made on demand */
	source->getVertDataArray(source, CD_ORIGINDEX);
	source->getEdgeDataArray(source, CD_ORIGINDEX);
	source->getTessFaceDataArray(source, CD_ORIGINDEX);

	/* this does a copy of all non mvert/medge/mface layers */
	DM_from_template(dm, source, DM_TYPE_CDDM, numVerts, numEdges, numFaces, numLoops, numPolys);

	/* now add mvert/medge/mface layers */
	CustomData_add_layer(&dm->vertData, CD_MVERT, CD_CALLOC, NULL, numVerts);
	CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_CALLOC, NULL, numEdges);
	CustomData_add_layer(&dm->faceData, CD_MFACE, CD_CALLOC, NULL, numFaces);
	CustomData_add_layer(&dm->loopData, CD_MLOOP, CD_CALLOC, NULL, numLoops);
	CustomData_add_layer(&dm->polyData, CD_MPOLY, CD_CALLOC, NULL, numPolys);

	if(!CustomData_get_layer(&dm->vertData, CD_ORIGINDEX))
		CustomData_add_layer(&dm->vertData, CD_ORIGINDEX, CD_CALLOC, NULL, numVerts);
	if(!CustomData_get_layer(&dm->edgeData, CD_ORIGINDEX))
		CustomData_add_layer(&dm->edgeData, CD_ORIGINDEX, CD_CALLOC, NULL, numEdges);
	if(!CustomData_get_layer(&dm->faceData, CD_ORIGINDEX))
		CustomData_add_layer(&dm->faceData, CD_ORIGINDEX, CD_CALLOC, NULL, numFaces);

	cddm->mvert = CustomData_get_layer(&dm->vertData, CD_MVERT);
	cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);
	cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);
	cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
	cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);

	return dm;
}

void CDDM_apply_vert_coords(DerivedMesh *dm, float (*vertCoords)[3])
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	MVert *vert;
	int i;

	/* this will just return the pointer if it wasn't a referenced layer */
	vert = CustomData_duplicate_referenced_layer(&dm->vertData, CD_MVERT);
	cddm->mvert = vert;

	for(i = 0; i < dm->numVertData; ++i, ++vert)
		VECCOPY(vert->co, vertCoords[i]);
}

void CDDM_apply_vert_normals(DerivedMesh *dm, short (*vertNormals)[3])
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	MVert *vert;
	int i;

	/* this will just return the pointer if it wasn't a referenced layer */
	vert = CustomData_duplicate_referenced_layer(&dm->vertData, CD_MVERT);
	cddm->mvert = vert;

	for(i = 0; i < dm->numVertData; ++i, ++vert)
		VECCOPY(vert->no, vertNormals[i]);
}

void CDDM_calc_normals(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	float (*face_nors)[3] = NULL;
	
	if(dm->numVertData == 0) return;

	/* we don't want to overwrite any referenced layers */
	cddm->mvert = CustomData_duplicate_referenced_layer(&dm->vertData, CD_MVERT);
	
	/*set tesselation origindex values to map to poly indices, rather then poly
	  poly origindex values*/
	cdDM_recalcTesselation2(dm);
	
	face_nors = MEM_mallocN(sizeof(float)*3*dm->numFaceData, "face_nors");
	
	/* calculate face normals */
	mesh_calc_normals(cddm->mvert, dm->numVertData, CDDM_get_loops(dm), CDDM_get_polys(dm), 
					  dm->numLoopData, dm->numPolyData, NULL, cddm->mface, dm->numFaceData, 
					  CustomData_get_layer(&dm->faceData, CD_ORIGINDEX), face_nors);
	
	/*restore tesselation origindex indices to poly origindex indices*/
	cdDM_recalcTesselation(dm);

	CustomData_add_layer(&dm->faceData, CD_NORMAL, CD_ASSIGN, 
		face_nors, dm->numFaceData);
}

#if 1
/*merge verts
 
  vtargetmap is a table that maps vertices to target vertices.  a value of -1
  indicates a vertex is a target, and is to be kept.
  
  this frees dm, and returns a new one.
  
  this is a really horribly written function.  ger. - joeedh

 */
DerivedMesh *CDDM_merge_verts(DerivedMesh *dm, int *vtargetmap)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	CDDerivedMesh *cddm2 = NULL;
	MVert *mv, *mvert = NULL;
	BLI_array_declare(mvert);
	MEdge *me, *medge = NULL;
	BLI_array_declare(medge);
	MPoly *mp, *mpoly = NULL;
	BLI_array_declare(mpoly);
	MLoop *ml, *mloop = NULL;
	BLI_array_declare(mloop);
	EdgeHash *ehash = BLI_edgehash_new();
	int *newv = NULL, *newe = NULL, *newl = NULL;
	int *oldv = NULL, *olde = NULL, *oldl = NULL, *oldp = NULL;
	BLI_array_declare(oldv); BLI_array_declare(olde); BLI_array_declare(oldl); BLI_array_declare(oldp);
	int i, j, c, totloop, totpoly;
	
	totloop = dm->numLoopData;
	totpoly = dm->numPolyData;
	
	newv = MEM_callocN(sizeof(int)*dm->numVertData, "newv vtable CDDM_merge_verts");
	newe = MEM_callocN(sizeof(int)*dm->numEdgeData, "newv etable CDDM_merge_verts");
	newl = MEM_callocN(sizeof(int)*totloop, "newv ltable CDDM_merge_verts");
	
	/*fill newl with destination vertex indices*/
	mv = cddm->mvert;
	c = 0;
	for (i=0; i<dm->numVertData; i++, mv++) {
		if (vtargetmap[i] == -1) {
			BLI_array_append(oldv, i);
			newv[i] = c++;
			BLI_array_append(mvert, *mv);
		}
	}
	
	/*now link target vertices to destination indices*/
	for (i=0; i<dm->numVertData; i++) {
		if (vtargetmap[i] != -1) {
			newv[i] = newv[vtargetmap[i]];
		}
	}
	
	/*find-replace merged vertices with target vertices*/	
	ml = cddm->mloop;
	c = 0;
	for (i=0; i<totloop; i++, ml++) {
		if (ml->v == -1)
			continue;
		
		if (vtargetmap[ml->v] != -1) {
			me = &cddm->medge[ml->e];
			if (me->v1 == ml->v)
				me->v1 = vtargetmap[ml->v];
			else
				me->v2 = vtargetmap[ml->v];
			
			ml->v = vtargetmap[ml->v];
		}
	}
	
	/*now go through and fix edges and faces*/
	me = cddm->medge;
	c = 0;
	for (i=0; i<dm->numEdgeData; i++, me++) {
		int v1, v2;
		
		if (me->v1 == me->v2) {
			newe[i] = -1;
			continue;
		}
		
		if (vtargetmap[me->v1] != -1)
			v1 = vtargetmap[me->v1];
		else
			v1 = me->v1;
		
		if (vtargetmap[me->v2] != -1)
			v2 = vtargetmap[me->v2];
		else
			v2 = me->v2;
		
		if (BLI_edgehash_haskey(ehash, v1, v2)) {
			newe[i] = GET_INT_FROM_POINTER(BLI_edgehash_lookup(ehash, v1, v2));
		} else {
			BLI_array_append(olde, i);
			newe[i] = c;
			BLI_array_append(medge, *me);
			BLI_edgehash_insert(ehash, v1, v2, SET_INT_IN_POINTER(c));
			c++;
		}
	}
	
	mp = cddm->mpoly;
	for (i=0; i<totpoly; i++, mp++) {
		MPoly *mp2;
		
		ml = cddm->mloop + mp->loopstart;
		
		c = 0;
		for (j=0; j<mp->totloop; j++, ml++) {
			if (ml->v == -1)
				continue;
			
			me = cddm->medge + ml->e;
			if (me->v1 != me->v2) {
				BLI_array_append(oldl, j+mp->loopstart);
				BLI_array_append(mloop, *ml);
				newl[j+mp->loopstart] = BLI_array_count(mloop)-1;
				c++;
			}
		}
		
		if (!c)
			continue;
		
		mp2 = BLI_array_append(mpoly, *mp);
		mp2->totloop = c;
		mp2->loopstart = BLI_array_count(mloop) - c;
		
		BLI_array_append(oldp, i);
	}
	
	/*create new cddm*/	
	cddm2 = (CDDerivedMesh*) CDDM_from_template((DerivedMesh*)cddm, BLI_array_count(mvert), BLI_array_count(medge), 0, BLI_array_count(mloop), BLI_array_count(mpoly));
	
	/*update edge indices and copy customdata*/
	me = medge;
	for (i=0; i<cddm2->dm.numEdgeData; i++, me++) {
		if (newv[me->v1] != -1)
			me->v1 = newv[me->v1];
		if (newv[me->v2] != -1)
			me->v2 = newv[me->v2];
		
		CustomData_copy_data(&dm->edgeData, &cddm2->dm.edgeData, olde[i], i, 1);
	}
	
	/*update loop indices and copy customdata*/
	ml = mloop;
	for (i=0; i<cddm2->dm.numLoopData; i++, ml++) {
		if (newe[ml->e] != -1)
			ml->e = newe[ml->e];
		if (newv[ml->v] != -1)
			ml->v = newv[ml->v];
			
		CustomData_copy_data(&dm->loopData, &cddm2->dm.loopData, oldl[i], i, 1);
	}
	
	/*copy vertex customdata*/	
	mv = mvert;
	for (i=0; i<cddm2->dm.numVertData; i++, mv++) {
		CustomData_copy_data(&dm->vertData, &cddm2->dm.vertData, oldv[i], i, 1);
	}
	
	/*copy poly customdata*/
	mp = mpoly;
	for (i=0; i<cddm2->dm.numPolyData; i++, mp++) {
		CustomData_copy_data(&dm->polyData, &cddm2->dm.polyData, oldp[i], i, 1);
	}
	
	/*copy over data.  CustomData_add_layer can do this, need to look it up.*/
	memcpy(cddm2->mvert, mvert, sizeof(MVert)*BLI_array_count(mvert));
	memcpy(cddm2->medge, medge, sizeof(MEdge)*BLI_array_count(medge));
	memcpy(cddm2->mloop, mloop, sizeof(MLoop)*BLI_array_count(mloop));
	memcpy(cddm2->mpoly, mpoly, sizeof(MPoly)*BLI_array_count(mpoly));
	BLI_array_free(mvert); BLI_array_free(medge); BLI_array_free(mloop); BLI_array_free(mpoly);

	CDDM_recalc_tesselation((DerivedMesh*)cddm2, 1);
	
	if (newv) 
		MEM_freeN(newv); 
	if (newe)
		MEM_freeN(newe); 
	if (newl)
		MEM_freeN(newl);
	if (oldv) 
		MEM_freeN(oldv); 
	if (olde) 
		MEM_freeN(olde); 
	if (oldl) 
		MEM_freeN(oldl); 
	if (oldp) 
		MEM_freeN(oldp);
	if (ehash)
		BLI_edgehash_free(ehash, NULL);

	/*free old derivedmesh*/
	dm->needsFree = 1;
	dm->release(dm);
	
	return (DerivedMesh*)cddm2;
}
#endif

void CDDM_calc_edges(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	CustomData edgeData;
	EdgeHashIterator *ehi;
	MFace *mf = cddm->mface;
	MEdge *med;
	EdgeHash *eh = BLI_edgehash_new();
	int i, *index, numEdges, maxFaces = dm->numFaceData;

	for (i = 0; i < maxFaces; i++, mf++) {
		if (!BLI_edgehash_haskey(eh, mf->v1, mf->v2))
			BLI_edgehash_insert(eh, mf->v1, mf->v2, NULL);
		if (!BLI_edgehash_haskey(eh, mf->v2, mf->v3))
			BLI_edgehash_insert(eh, mf->v2, mf->v3, NULL);
		
		if (mf->v4) {
			if (!BLI_edgehash_haskey(eh, mf->v3, mf->v4))
				BLI_edgehash_insert(eh, mf->v3, mf->v4, NULL);
			if (!BLI_edgehash_haskey(eh, mf->v4, mf->v1))
				BLI_edgehash_insert(eh, mf->v4, mf->v1, NULL);
		} else {
			if (!BLI_edgehash_haskey(eh, mf->v3, mf->v1))
				BLI_edgehash_insert(eh, mf->v3, mf->v1, NULL);
		}
	}

	numEdges = BLI_edgehash_size(eh);

	/* write new edges into a temporary CustomData */
	memset(&edgeData, 0, sizeof(edgeData));
	CustomData_add_layer(&edgeData, CD_MEDGE, CD_CALLOC, NULL, numEdges);
	CustomData_add_layer(&edgeData, CD_ORIGINDEX, CD_CALLOC, NULL, numEdges);

	ehi = BLI_edgehashIterator_new(eh);
	med = CustomData_get_layer(&edgeData, CD_MEDGE);
	index = CustomData_get_layer(&edgeData, CD_ORIGINDEX);
	for(i = 0; !BLI_edgehashIterator_isDone(ehi);
		BLI_edgehashIterator_step(ehi), ++i, ++med, ++index) {
		BLI_edgehashIterator_getKey(ehi, (int*)&med->v1, (int*)&med->v2);

		med->flag = ME_EDGEDRAW|ME_EDGERENDER;
		*index = ORIGINDEX_NONE;
	}
	BLI_edgehashIterator_free(ehi);

	/* free old CustomData and assign new one */
	CustomData_free(&dm->edgeData, dm->numEdgeData);
	dm->edgeData = edgeData;
	dm->numEdgeData = numEdges;

	cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);

	BLI_edgehash_free(eh, NULL);
}


void CDDM_calc_edges_poly(DerivedMesh *dm)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	CustomData edgeData;
	EdgeHashIterator *ehi;
	MPoly *mp = cddm->mpoly;
	MLoop *ml;
	MEdge *med;
	EdgeHash *eh = BLI_edgehash_new();
	int v1, v2;
	int *eindex;
	int i, j, k, *index, numEdges = cddm->dm.numEdgeData, maxFaces = dm->numPolyData;

	eindex = DM_get_edge_data_layer(dm, CD_ORIGINDEX);

	med = cddm->medge;
	if (med) {
		for (i=0; i < numEdges; i++, med++) {
			BLI_edgehash_insert(eh, med->v1, med->v2, SET_INT_IN_POINTER(i+1));
		}
	}

	for (i=0; i < maxFaces; i++, mp++) {
		ml = cddm->mloop + mp->loopstart;
		for (j=0; j<mp->totloop; j++, ml++) {
			v1 = ml->v;
			v2 = (cddm->mloop + mp->loopstart + ((j+1)%mp->totloop))->v;
			if (!BLI_edgehash_haskey(eh, v1, v2)) {
				BLI_edgehash_insert(eh, v1, v2, NULL);
			}
		}
	}

	k = numEdges;
	numEdges = BLI_edgehash_size(eh);

	/* write new edges into a temporary CustomData */
	memset(&edgeData, 0, sizeof(edgeData));
	CustomData_add_layer(&edgeData, CD_MEDGE, CD_CALLOC, NULL, numEdges);
	CustomData_add_layer(&edgeData, CD_ORIGINDEX, CD_CALLOC, NULL, numEdges);

	ehi = BLI_edgehashIterator_new(eh);
	med = CustomData_get_layer(&edgeData, CD_MEDGE);
	index = CustomData_get_layer(&edgeData, CD_ORIGINDEX);
	for(i = 0; !BLI_edgehashIterator_isDone(ehi);
	    BLI_edgehashIterator_step(ehi), ++i, ++med, ++index) {
		BLI_edgehashIterator_getKey(ehi, (int*)&med->v1, (int*)&med->v2);
		j = GET_INT_FROM_POINTER(BLI_edgehashIterator_getValue(ehi));

		med->flag = ME_EDGEDRAW|ME_EDGERENDER;
		*index = j==0 ? ORIGINDEX_NONE : eindex[j-1];

		BLI_edgehashIterator_setValue(ehi, SET_INT_IN_POINTER(i));
	}
	BLI_edgehashIterator_free(ehi);

	/* free old CustomData and assign new one */
	CustomData_free(&dm->edgeData, dm->numEdgeData);
	dm->edgeData = edgeData;
	dm->numEdgeData = numEdges;

	cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);

	mp = cddm->mpoly;
	for (i=0; i < maxFaces; i++, mp++) {
		ml = cddm->mloop + mp->loopstart;
		for (j=0; j<mp->totloop; j++, ml++) {
			v1 = ml->v;
			v2 = (cddm->mloop + mp->loopstart + ((j+1)%mp->totloop))->v;
			ml->e = GET_INT_FROM_POINTER(BLI_edgehash_lookup(eh, v1, v2));
		}
	}

	BLI_edgehash_free(eh, NULL);
}

void CDDM_lower_num_verts(DerivedMesh *dm, int numVerts)
{
	if (numVerts < dm->numVertData)
		CustomData_free_elem(&dm->vertData, numVerts, dm->numVertData-numVerts);

	dm->numVertData = numVerts;
}

void CDDM_lower_num_edges(DerivedMesh *dm, int numEdges)
{
	if (numEdges < dm->numEdgeData)
		CustomData_free_elem(&dm->edgeData, numEdges, dm->numEdgeData-numEdges);

	dm->numEdgeData = numEdges;
}

void CDDM_lower_num_faces(DerivedMesh *dm, int numFaces)
{
	if (numFaces < dm->numFaceData)
		CustomData_free_elem(&dm->faceData, numFaces, dm->numFaceData-numFaces);

	dm->numFaceData = numFaces;
}

MVert *CDDM_get_vert(DerivedMesh *dm, int index)
{
	return &((CDDerivedMesh*)dm)->mvert[index];
}

MEdge *CDDM_get_edge(DerivedMesh *dm, int index)
{
	return &((CDDerivedMesh*)dm)->medge[index];
}

MFace *CDDM_get_tessface(DerivedMesh *dm, int index)
{
	return &((CDDerivedMesh*)dm)->mface[index];
}

MVert *CDDM_get_verts(DerivedMesh *dm)
{
	return ((CDDerivedMesh*)dm)->mvert;
}

MEdge *CDDM_get_edges(DerivedMesh *dm)
{
	return ((CDDerivedMesh*)dm)->medge;
}

MFace *CDDM_get_tessfaces(DerivedMesh *dm)
{
	return ((CDDerivedMesh*)dm)->mface;
}

MLoop *CDDM_get_loops(DerivedMesh *dm)
{
	return ((CDDerivedMesh*)dm)->mloop;
}

MPoly *CDDM_get_polys(DerivedMesh *dm)
{
	return ((CDDerivedMesh*)dm)->mpoly;
}

void CDDM_tessfaces_to_faces(DerivedMesh *dm)
{
	/*converts mfaces to mpolys/mloops*/
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	MFace *mf;
	MEdge *me;
	MLoop *ml;
	MPoly *mp;
	EdgeHash *eh = BLI_edgehash_new();
	int i, l, totloop, *index1, *index2;
	
	/*ensure we have all the edges we need*/
	CDDM_calc_edges(dm);

	/*build edge hash*/
	me = cddm->medge;
	for (i=0; i<cddm->dm.numEdgeData; i++, me++) {
		BLI_edgehash_insert(eh, me->v1, me->v2, SET_INT_IN_POINTER(i));
	}

	mf = cddm->mface;
	totloop = 0;
	for (i=0; i<cddm->dm.numFaceData; i++, mf++) {
		totloop += mf->v4 ? 4 : 3;
	}

	CustomData_free(&cddm->dm.polyData, cddm->dm.numPolyData);
	CustomData_free(&cddm->dm.loopData, cddm->dm.numLoopData);
	
	cddm->dm.numLoopData = totloop;
	cddm->dm.numPolyData = cddm->dm.numFaceData;

	if (!totloop) return;

	cddm->mloop = MEM_callocN(sizeof(MLoop)*totloop, "cddm->mloop in CDDM_tessfaces_to_faces");
	cddm->mpoly = MEM_callocN(sizeof(MPoly)*cddm->dm.numFaceData, "cddm->mpoly in CDDM_tessfaces_to_faces");
	
	CustomData_add_layer(&cddm->dm.loopData, CD_MLOOP, CD_ASSIGN, cddm->mloop, totloop);
	CustomData_add_layer(&cddm->dm.polyData, CD_MPOLY, CD_ASSIGN, cddm->mpoly, cddm->dm.numPolyData);
	CustomData_merge(&cddm->dm.faceData, &cddm->dm.polyData, 
		CD_MASK_ORIGINDEX, CD_DUPLICATE, cddm->dm.numFaceData);

	index1 = CustomData_get_layer(&cddm->dm.faceData, CD_ORIGINDEX);
	index2 = CustomData_get_layer(&cddm->dm.polyData, CD_ORIGINDEX);

	mf = cddm->mface;
	mp = cddm->mpoly;
	ml = cddm->mloop;
	l = 0;
	for (i=0; i<cddm->dm.numFaceData; i++, mf++, mp++) {
		mp->flag = mf->flag;
		mp->loopstart = l;
		mp->mat_nr = mf->mat_nr;
		mp->totloop = mf->v4 ? 4 : 3;
		
		ml->v = mf->v1;
		ml->e = GET_INT_FROM_POINTER(BLI_edgehash_lookup(eh, mf->v1, mf->v2));
		ml++, l++;

		ml->v = mf->v2;
		ml->e = GET_INT_FROM_POINTER(BLI_edgehash_lookup(eh, mf->v2, mf->v3));
		ml++, l++;

		ml->v = mf->v3;
		ml->e = GET_INT_FROM_POINTER(BLI_edgehash_lookup(eh, mf->v3, mf->v4?mf->v4:mf->v1));
		ml++, l++;

		if (mf->v4) {
			ml->v = mf->v4;
			ml->e =	GET_INT_FROM_POINTER(BLI_edgehash_lookup(eh, mf->v4, mf->v1));
			ml++, l++;
		}

	}

	BLI_edgehash_free(eh, NULL);
}

void CDDM_set_mvert(DerivedMesh *dm, MVert *mvert)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;
	
	if (!CustomData_has_layer(&dm->vertData, CD_MVERT))
		CustomData_add_layer(&dm->vertData, CD_MVERT, CD_ASSIGN, mvert, dm->numVertData);
				
	cddm->mvert = mvert;
}

void CDDM_set_medge(DerivedMesh *dm, MEdge *medge)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;

	if (!CustomData_has_layer(&dm->edgeData, CD_MEDGE))
		CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_ASSIGN, medge, dm->numEdgeData);

	cddm->medge = medge;
}

void CDDM_set_mface(DerivedMesh *dm, MFace *mface)
{
	CDDerivedMesh *cddm = (CDDerivedMesh*)dm;

	if (!CustomData_has_layer(&dm->faceData, CD_MFACE))
		CustomData_add_layer(&dm->faceData, CD_MFACE, CD_ASSIGN, mface, dm->numFaceData);

	cddm->mface = mface;
}
