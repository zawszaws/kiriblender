#include "MEM_guardedalloc.h"

#include "BKE_utildefines.h"

#include "bmesh.h"
#include "mesh_intern.h"
#include "bmesh_private.h"
#include "BLI_math.h"
#include "BLI_array.h"

#include <stdio.h>
#include <string.h>

#define VERT_INPUT	1
#define EDGE_OUT	1
#define FACE_NEW	2
#define EDGE_MARK	4
#define EDGE_DONE	8

void connectverts_exec(BMesh *bm, BMOperator *op)
{
	BMIter iter, liter;
	BMFace *f, *nf;
	BMLoop **loops = NULL, *lastl = NULL;
	BLI_array_declare(loops);
	BMLoop *l, *nl;
	BMVert **verts = NULL;
	BLI_array_declare(verts);
	int i;
	
	BMO_Flag_Buffer(bm, op, "verts", VERT_INPUT, BM_VERT);

	for (f=BMIter_New(&iter, bm, BM_FACES_OF_MESH, NULL); f; f=BMIter_Step(&iter)){
		BLI_array_empty(loops);
		BLI_array_empty(verts);
		
		if (BMO_TestFlag(bm, f, FACE_NEW)) continue;

		l = BMIter_New(&liter, bm, BM_LOOPS_OF_FACE, f);
		lastl = NULL;
		for (; l; l=BMIter_Step(&liter)) {
			if (BMO_TestFlag(bm, l->v, VERT_INPUT)) {
				if (!lastl) {
					lastl = l;
					continue;
				}

				if (lastl != l->prev && lastl != 
				    l->next)
				{
					BLI_array_growone(loops);
					loops[BLI_array_count(loops)-1] = lastl;

					BLI_array_growone(loops);
					loops[BLI_array_count(loops)-1] = l;

				}
				lastl = l;
			}
		}

		if (BLI_array_count(loops) == 0) continue;
		
		if (BLI_array_count(loops) > 2) {
			BLI_array_growone(loops);
			loops[BLI_array_count(loops)-1] = loops[BLI_array_count(loops)-2];

			BLI_array_growone(loops);
			loops[BLI_array_count(loops)-1] = loops[0];
		}

		BM_LegalSplits(bm, f, (BMLoop *(*)[2])loops, BLI_array_count(loops)/2);
		
		for (i=0; i<BLI_array_count(loops)/2; i++) {
			if (loops[i*2]==NULL) continue;

			BLI_array_growone(verts);
			verts[BLI_array_count(verts)-1] = loops[i*2]->v;
		
			BLI_array_growone(verts);
			verts[BLI_array_count(verts)-1] = loops[i*2+1]->v;
		}

		for (i=0; i<BLI_array_count(verts)/2; i++) {
			nf = BM_Split_Face(bm, f, verts[i*2],
				           verts[i*2+1], &nl, NULL);
			f = nf;
			
			if (!nl || !nf) {
				BMO_RaiseError(bm, op,
					BMERR_CONNECTVERT_FAILED, NULL);
				BLI_array_free(loops);
				return;
			}
			BMO_SetFlag(bm, nf, FACE_NEW);
			BMO_SetFlag(bm, nl->e, EDGE_OUT);
		}
	}

	BMO_Flag_To_Slot(bm, op, "edgeout", EDGE_OUT, BM_EDGE);

	BLI_array_free(loops);
	BLI_array_free(verts);
}

static BMVert *get_outer_vert(BMesh *bm, BMEdge *e) 
{
	BMIter iter;
	BMEdge *e2;
	int i;
	
	i= 0;
	BM_ITER(e2, &iter, bm, BM_EDGES_OF_VERT, e->v1) {
		if (BMO_TestFlag(bm, e2, EDGE_MARK))
			i++;
	}
	
	if (i==2) 
		return e->v2;
	else
		return e->v1;
}

/* Clamp x to the interval {0..len-1}, with wrap-around */
#ifdef CLAMP_INDEX
#undef CLAMP_INDEX
#endif
#define CLAMP_INDEX(x, len) (((x) < 0) ? (len - (-(x) % len)) : ((x) % len))

/* There probably is a better way to swap BLI_arrays, or if there
   isn't there should be... */
#define ARRAY_SWAP(elemtype, arr1, arr2)                                      \
	{                                                                         \
		int i;                                                                \
		elemtype *arr_tmp = NULL;                                             \
		BLI_array_declare(arr_tmp);                                           \
		for (i = 0; i < BLI_array_count(arr1); i++) {                         \
			BLI_array_append(arr_tmp, arr1[i]);                               \
		}                                                                     \
		BLI_array_empty(arr1);                                                \
		for (i = 0; i < BLI_array_count(arr2); i++) {                         \
			BLI_array_append(arr1, arr2[i]);                                  \
		}                                                                     \
		BLI_array_empty(arr2);                                                \
		for (i = 0; i < BLI_array_count(arr_tmp); i++) {                      \
			BLI_array_append(arr2, arr_tmp[i]);                               \
		}                                                                     \
		BLI_array_free(arr_tmp);                                              \
	}

void bmesh_bridge_loops_exec(BMesh *bm, BMOperator *op)
{
	BMEdge **ee1 = NULL, **ee2 = NULL;
	BMVert **vv1 = NULL, **vv2 = NULL;
	BLI_array_declare(ee1);
	BLI_array_declare(ee2);
	BLI_array_declare(vv1);
	BLI_array_declare(vv2);
	BMOIter siter;
	BMIter iter;
	BMEdge *e, *nexte;
	int c=0, cl1=0, cl2=0;

	BMO_Flag_Buffer(bm, op, "edges", EDGE_MARK, BM_EDGE);

	BMO_ITER(e, &siter, bm, op, "edges", BM_EDGE) {
		if (!BMO_TestFlag(bm, e, EDGE_DONE)) {
			BMVert *v, *ov;
			/* BMEdge *e2, *e3, *oe = e; */ /* UNUSED */
			BMEdge *e2, *e3;
			
			if (c > 2) {
				BMO_RaiseError(bm, op, BMERR_INVALID_SELECTION, "Select only two edge loops");
				goto cleanup;
			}
			
			e2 = e;
			v = e->v1;
			do {
				v = BM_OtherEdgeVert(e2, v);
				nexte = NULL;
				BM_ITER(e3, &iter, bm, BM_EDGES_OF_VERT, v) {
					if (e3 != e2 && BMO_TestFlag(bm, e3, EDGE_MARK)) {
						if (nexte == NULL) {
							nexte = e3;
						}
						else {
							/* edges do not form a loop: there is a disk
							   with more than two marked edges. */
							BMO_RaiseError(bm, op, BMERR_INVALID_SELECTION,
								"Selection must only contain edges from two edge loops");
							goto cleanup;
						}
					}
				}
				
				if (nexte)
					e2 = nexte;
			} while (nexte && e2 != e);
			
			if (!e2)
				e2 = e;
				
			e = e2;
			ov = v;
			do {
				if (c==0) {
					BLI_array_append(ee1, e2);
					BLI_array_append(vv1, v);
				} else {
					BLI_array_append(ee2, e2);
					BLI_array_append(vv2, v);
				}
				
				BMO_SetFlag(bm, e2, EDGE_DONE);
				
				v = BM_OtherEdgeVert(e2, v);
				BM_ITER(e3, &iter, bm, BM_EDGES_OF_VERT, v) {
					if (e3 != e2 && BMO_TestFlag(bm, e3, EDGE_MARK) && !BMO_TestFlag(bm, e3, EDGE_DONE)) {
						break;
					}
				}
				if (e3)
					e2 = e3;
			} while (e3 && e2 != e);
			
			if (v && !e3) {			
				if (c==0) {
					if (BLI_array_count(vv1) && v == vv1[BLI_array_count(vv1)-1]) {
						printf("%s: internal state waning *TODO DESCRIPTION!*\n", __func__);
					}
					BLI_array_append(vv1, v);
				} else {
					BLI_array_append(vv2, v);
				}
			}
			
			/*test for connected loops, and set cl1 or cl2 if so*/
			if (v == ov) {
				if (c==0)
					cl1 = 1;
				else 
					cl2 = 1;
			}
			
			c++;
		}
	}

	if (ee1 && ee2) {
		int i, j;
		BMVert *v1, *v2, *v3, *v4;
		int starti=0, dir1=1, wdir=0, lenv1, lenv2;

		/* Simplify code below by avoiding the (!cl1 && cl2) case */
		if (!cl1 && cl2) {
			SWAP(int, cl1, cl2);
			ARRAY_SWAP(BMVert *, vv1, vv2);
			ARRAY_SWAP(BMEdge *, ee1, ee2);
		}

		lenv1=BLI_array_count(vv1);
		lenv2=BLI_array_count(vv1);

		/* Below code assumes vv1/vv2 each have at least two verts. should always be
		   a safe assumption, since ee1/ee2 are non-empty and an edge has two verts. */
		BLI_assert((lenv1 > 1) && (lenv2 > 1));

		/* BMESH_TODO: Would be nice to handle cases where the edge loops
		   have different edge counts by generating triangles & quads for
		   the bridge instead of quads only. */
		if (BLI_array_count(ee1) != BLI_array_count(ee2)) {
			BMO_RaiseError(bm, op, BMERR_INVALID_SELECTION,
				"Selected loops must have equal edge counts");
			goto cleanup;
		}

		j = 0;
		if (vv1[0] == vv1[lenv1-1]) {
			lenv1--;
		}
		if (vv2[0] == vv2[lenv2-1]) {
			lenv2--;
		}

		/* Find starting point and winding direction for two unclosed loops */
		if (!cl1 && !cl2) {
			/* First point of loop 1 */
			v1 = get_outer_vert(bm, ee1[0]);
			/* Last point of loop 1 */
			v2 = get_outer_vert(bm, ee1[CLAMP_INDEX(-1, BLI_array_count(ee1))]);
			/* First point of loop 2 */
			v3 = get_outer_vert(bm, ee2[0]);
			/* Last point of loop 2 */
			v4 = get_outer_vert(bm, ee2[CLAMP_INDEX(-1, BLI_array_count(ee2))]);

			/* If v1 is a better match for v4 than v3, AND v2 is a better match
			   for v3 than v4, the loops are in opposite directions, so reverse
			   the order of reads from vv1. We can avoid sqrt for comparison */
			if (len_squared_v3v3(v1->co, v3->co) > len_squared_v3v3(v1->co, v4->co) &&
				len_squared_v3v3(v2->co, v4->co) > len_squared_v3v3(v2->co, v3->co))
			{
				dir1 = -1;
				starti = CLAMP_INDEX(-1, lenv1);
			}
		}

		/* Find the shortest distance from a vert in vv1 to vv2[0]. Use that
		   vertex in vv1 as a starting point in the first loop, while starting
		   from vv2[0] in the second loop. This is a simplistic attempt to get
		   a better edge-to-edge match between the two loops. */
		if (cl1) {
			int previ, nexti;
			float min = 1e32;

			/* BMESH_TODO: Would be nice to do a more thorough analysis of all
			   the vertices in both loops to find a more accurate match for the
			   starting point and winding direction of the bridge generation. */
			
			for (i=0; i<BLI_array_count(vv1); i++) {
				if (len_v3v3(vv1[i]->co, vv2[0]->co) < min) {
					min = len_v3v3(vv1[i]->co, vv2[0]->co);
					starti = i;
				}
			}

			/* Reverse iteration order for the first loop if the distance of
			 * the (starti-1) vert from vv1 is a better match for vv2[1] than
			 * the (starti+1) vert.
			 *
			 * This is not always going to be right, but it will work better in
			 * the average case.
			 */
			previ = CLAMP_INDEX(starti - 1, lenv1);
			nexti = CLAMP_INDEX(starti + 1, lenv1);

			/* avoid sqrt for comparison */
			if (len_squared_v3v3(vv1[nexti]->co, vv2[1]->co) > len_squared_v3v3(vv1[previ]->co, vv2[1]->co)) {
				/* reverse direction for reading vv1 (1 is forward, -1 is backward) */
				dir1 = -1;
			}
		}

		/* Vert rough attempt to determine proper winding for the bridge quads:
		   just uses the first loop it finds for any of the edges of ee2 or ee1 */
		if (wdir == 0) {
			for (i=0; i<BLI_array_count(ee2); i++) {
				if (ee2[i]->l) {
					wdir = (ee2[i]->l->v == vv2[i]) ? (-1) : (1);
					break;
				}
			}
		}
		if (wdir == 0) {
			for (i=0; i<BLI_array_count(ee1); i++) {
				j = CLAMP_INDEX((i*dir1)+starti, lenv1);
				if (ee1[j]->l && ee2[j]->l) {
					wdir = (ee2[j]->l->v == vv2[j]) ? (1) : (-1);
					break;
				}
			}
		}
		
		/* Generate the bridge quads */
		for (i=0; i<BLI_array_count(ee1) && i<BLI_array_count(ee2); i++) {
			BMFace *f;
			int i1, i1next, i2, i2next;

			i1 = CLAMP_INDEX(i*dir1 + starti, lenv1);
			i1next = CLAMP_INDEX((i+1)*dir1 + starti, lenv1);
			i2 = i;
			i2next = CLAMP_INDEX(i+1, lenv2);
		
			if (vv1[i1] ==  vv1[i1next]) {
				continue;
			}

			if (wdir < 0) {
				SWAP(int, i1, i1next);
				SWAP(int, i2, i2next);
			}

			f = BM_Make_QuadTri(bm, 
				vv1[i1],
				vv2[i2],
				vv2[i2next],
				vv1[i1next],
				NULL, 1);
			if (!f || f->len != 4) {
				fprintf(stderr, "%s: in bridge! (bmesh internal error)\n", __func__);
			}
		}
	}

cleanup:
	BLI_array_free(ee1);
	BLI_array_free(ee2);
	BLI_array_free(vv1);
	BLI_array_free(vv2);
}
