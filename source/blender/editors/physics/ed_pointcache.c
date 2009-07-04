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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2007 by Janne Karhu.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"
#include "DNA_object_force.h"
#include "DNA_modifier_types.h"

#include "BKE_context.h"
#include "BKE_particle.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_utildefines.h" 
#include "BKE_pointcache.h"
#include "BKE_global.h"
#include "BKE_modifier.h"

#include "BLI_blenlib.h"

#include "ED_screen.h"
#include "ED_physics.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "physics_intern.h"

static int cache_break_test(void *cbd) {
	return G.afbreek==1;
}
/**************************** general **********************************/
static int ptcache_bake_all_poll(bContext *C)
{
	Scene *scene= CTX_data_scene(C);

	if(!scene)
		return 0;
	
	return 1;
}

static int ptcache_bake_all_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	PTCacheBaker baker;


	baker.scene = scene;
	baker.pid = NULL;
	baker.bake = RNA_boolean_get(op->ptr, "bake");
	baker.render = 0;
	baker.anim_init = 0;
	baker.quick_step = 1;
	baker.break_test = cache_break_test;
	baker.break_data = NULL;
	baker.progressbar = (void (*)(void *, int))WM_timecursor;
	baker.progresscontext = CTX_wm_window(C);

	BKE_ptcache_make_cache(&baker);

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}
static int ptcache_free_bake_all_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Base *base;
	PTCacheID *pid;
	ListBase pidlist;

	for(base=scene->base.first; base; base= base->next) {
		BKE_ptcache_ids_from_object(&pidlist, base->object);

		for(pid=pidlist.first; pid; pid=pid->next) {
			pid->cache->flag &= ~PTCACHE_BAKED;
		}
		
		BLI_freelistN(&pidlist);
	}

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}

void PTCACHE_OT_bake_all(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Bake All Physics";
	ot->idname= "PTCACHE_OT_bake_all";
	
	/* api callbacks */
	ot->exec= ptcache_bake_all_exec;
	ot->poll= ptcache_bake_all_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "bake", 0, "Bake", "");
}
void PTCACHE_OT_free_bake_all(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Free All Physics Bakes";
	ot->idname= "PTCACHE_OT_free_bake_all";
	
	/* api callbacks */
	ot->exec= ptcache_free_bake_all_exec;
	ot->poll= ptcache_bake_all_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/**************************** cloth **********************************/
static int ptcache_bake_cloth_poll(bContext *C)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);
	ClothModifierData *clmd = (ClothModifierData *)modifiers_findByType(ob, eModifierType_Cloth);

	if(!scene || !ob || ob->id.lib || !clmd)
		return 0;
	
	return 1;
}

static int ptcache_bake_cloth_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);
	ClothModifierData *clmd = (ClothModifierData *)modifiers_findByType(ob, eModifierType_Cloth);
	PTCacheID pid;
	PTCacheBaker baker;

	BKE_ptcache_id_from_cloth(&pid, ob, clmd);

	baker.scene = scene;
	baker.pid = &pid;
	baker.bake = RNA_boolean_get(op->ptr, "bake");
	baker.render = 0;
	baker.anim_init = 0;
	baker.quick_step = 1;
	baker.break_test = cache_break_test;
	baker.break_data = NULL;
	baker.progressbar = WM_timecursor;
	baker.progresscontext = CTX_wm_window(C);

	BKE_ptcache_make_cache(&baker);

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}
static int ptcache_free_bake_cloth_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);
	ClothModifierData *clmd = (ClothModifierData *)modifiers_findByType(ob, eModifierType_Cloth);
	PTCacheID pid;

	BKE_ptcache_id_from_cloth(&pid, ob, clmd);
	pid.cache->flag &= ~PTCACHE_BAKED;

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}
void PTCACHE_OT_cache_cloth(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Bake Cloth";
	ot->idname= "PTCACHE_OT_cache_cloth";
	
	/* api callbacks */
	ot->exec= ptcache_bake_cloth_exec;
	ot->poll= ptcache_bake_cloth_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "bake", 0, "Bake", "");
}
void PTCACHE_OT_free_bake_cloth(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Free Cloth Bake";
	ot->idname= "PTCACHE_OT_free_bake_cloth";
	
	/* api callbacks */
	ot->exec= ptcache_free_bake_cloth_exec;
	ot->poll= ptcache_bake_cloth_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}
static int ptcache_bake_from_cloth_cache_exec(bContext *C, wmOperator *op)
{
	Object *ob= CTX_data_active_object(C);
	ClothModifierData *clmd = (ClothModifierData *)modifiers_findByType(ob, eModifierType_Cloth);
	PTCacheID pid;

	BKE_ptcache_id_from_cloth(&pid, ob, clmd);
	pid.cache->flag |= PTCACHE_BAKED;

	return OPERATOR_FINISHED;
}
void PTCACHE_OT_bake_from_cloth_cache(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Bake From Cache";
	ot->idname= "PTCACHE_OT_bake_from_cloth_cache";
	
	/* api callbacks */
	ot->exec= ptcache_bake_from_cloth_cache_exec;
	ot->poll= ptcache_bake_cloth_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/**************************** particles **********************************/
static int ptcache_bake_particle_system_poll(bContext *C)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);

	if(!scene || !ob || ob->id.lib)
		return 0;
	
	return (ob->particlesystem.first != NULL);
}

static int ptcache_bake_particle_system_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);
	ParticleSystem *psys =psys_get_current(ob);
	PTCacheID pid;
	PTCacheBaker baker;

	BKE_ptcache_id_from_particles(&pid, ob, psys);

	baker.scene = scene;
	baker.pid = &pid;
	baker.bake = RNA_boolean_get(op->ptr, "bake");
	baker.render = 0;
	baker.anim_init = 0;
	baker.quick_step = 1;
	baker.break_test = cache_break_test;
	baker.break_data = NULL;
	baker.progressbar = (void (*)(void *, int))WM_timecursor;
	baker.progresscontext = CTX_wm_window(C);

	BKE_ptcache_make_cache(&baker);

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}
static int ptcache_free_bake_particle_system_exec(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	Object *ob= CTX_data_active_object(C);
	ParticleSystem *psys= psys_get_current(ob);
	PTCacheID pid;

	BKE_ptcache_id_from_particles(&pid, ob, psys);
	psys->pointcache->flag &= ~PTCACHE_BAKED;

	WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene);

	return OPERATOR_FINISHED;
}
void PTCACHE_OT_cache_particle_system(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Bake Particles";
	ot->idname= "PTCACHE_OT_cache_particle_system";
	
	/* api callbacks */
	ot->exec= ptcache_bake_particle_system_exec;
	ot->poll= ptcache_bake_particle_system_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "bake", 0, "Bake", "");
}
void PTCACHE_OT_free_bake_particle_system(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Free Particles Bake";
	ot->idname= "PTCACHE_OT_free_bake_particle_system";
	
	/* api callbacks */
	ot->exec= ptcache_free_bake_particle_system_exec;
	ot->poll= ptcache_bake_particle_system_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}
static int ptcache_bake_from_particles_cache_exec(bContext *C, wmOperator *op)
{
	Object *ob= CTX_data_active_object(C);
	ParticleSystem *psys= psys_get_current(ob);
	PTCacheID pid;

	BKE_ptcache_id_from_particles(&pid, ob, psys);
	psys->pointcache->flag |= PTCACHE_BAKED;

	return OPERATOR_FINISHED;
}
void PTCACHE_OT_bake_from_particles_cache(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Bake From Cache";
	ot->idname= "PTCACHE_OT_bake_from_particles_cache";
	
	/* api callbacks */
	ot->exec= ptcache_bake_from_particles_cache_exec;
	ot->poll= ptcache_bake_particle_system_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/**************************** registration **********************************/

void ED_operatortypes_pointcache(void)
{
	WM_operatortype_append(PTCACHE_OT_bake_all);
	WM_operatortype_append(PTCACHE_OT_free_bake_all);
	WM_operatortype_append(PTCACHE_OT_cache_particle_system);
	WM_operatortype_append(PTCACHE_OT_free_bake_particle_system);
	WM_operatortype_append(PTCACHE_OT_bake_from_particles_cache);
	WM_operatortype_append(PTCACHE_OT_cache_cloth);
	WM_operatortype_append(PTCACHE_OT_free_bake_cloth);
	WM_operatortype_append(PTCACHE_OT_bake_from_cloth_cache);
}

//void ED_keymap_pointcache(wmWindowManager *wm)
//{
//	ListBase *keymap= WM_keymap_listbase(wm, "Pointcache", 0, 0);
//	
//	WM_keymap_add_item(keymap, "PHYSICS_OT_bake_all", AKEY, KM_PRESS, 0, 0);
//	WM_keymap_add_item(keymap, "PHYSICS_OT_free_all", PADPLUSKEY, KM_PRESS, KM_CTRL, 0);
//	WM_keymap_add_item(keymap, "PHYSICS_OT_bake_particle_system", PADMINUS, KM_PRESS, KM_CTRL, 0);
//	WM_keymap_add_item(keymap, "PHYSICS_OT_free_particle_system", LKEY, KM_PRESS, 0, 0);
//}

