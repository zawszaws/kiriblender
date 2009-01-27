/**
 * $Id:
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
 * The Original Code is Copyright (C) 2007 Blender Foundation.
 * All rights reserved.
 *
 * 
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */
#ifndef WM_API_H
#define WM_API_H

/* dna-savable wmStructs here */
#include "DNA_windowmanager_types.h"

struct bContext;
struct IDProperty;
struct wmEvent;
struct wmEventHandler;
struct wmGesture;
struct wmJob;
struct wmNotifier;
struct rcti;
struct PointerRNA;

typedef struct wmJob wmJob;

/* general API */
void		WM_setprefsize		(int stax, int stay, int sizx, int sizy);

void		WM_init				(struct bContext *C);
void		WM_exit				(struct bContext *C);
void		WM_main				(struct bContext *C);

struct wmWindow	*WM_window_open		(struct bContext *C, struct rcti *rect);

			/* files */
int			WM_read_homefile	(struct bContext *C, int from_memory);
int			WM_write_homefile	(struct bContext *C, struct wmOperator *op);
void		WM_read_file		(struct bContext *C, char *name, struct ReportList *reports);
void		WM_write_file		(struct bContext *C, char *target, struct ReportList *reports);
void		WM_read_autosavefile(struct bContext *C);
void		WM_write_autosave	(struct bContext *C);

			/* mouse cursors */
void		WM_cursor_set		(struct wmWindow *win, int curs);
void		WM_cursor_modal		(struct wmWindow *win, int curs);
void		WM_cursor_restore	(struct wmWindow *win);
void		WM_cursor_wait		(struct wmWindow *win, int val);
void		WM_timecursor		(struct wmWindow *win, int nr);

void		*WM_paint_cursor_activate(struct wmWindowManager *wm, int (*poll)(struct bContext *C), void (*draw)(struct bContext *C, int, int, void *customdata), void *customdata);
void		WM_paint_cursor_end(struct wmWindowManager *wm, void *handle);

			/* keymap */
wmKeymapItem *WM_keymap_set_item	(ListBase *lb, char *idname, short type, 
								 short val, int modifier, short keymodifier);
wmKeymapItem *WM_keymap_verify_item(ListBase *lb, char *idname, short type, 
								 short val, int modifier, short keymodifier);
wmKeymapItem *WM_keymap_add_item	(ListBase *lb, char *idname, short type, 
								 short val, int modifier, short keymodifier);
ListBase	*WM_keymap_listbase	(struct wmWindowManager *wm, const char *nameid, 
								 int spaceid, int regionid);

char		*WM_key_event_string(short type);
char		*WM_key_event_operator_string(const struct bContext *C, const char *opname, int opcontext, char *str, int len);

			/* handlers */

struct wmEventHandler *WM_event_add_keymap_handler(ListBase *handlers, ListBase *keymap);
						/* boundbox, optional subwindow boundbox for offset */
struct wmEventHandler *WM_event_add_keymap_handler_bb(ListBase *handlers, ListBase *keymap, rcti *bb, rcti *swinbb);
						/* priority not implemented, it adds in begin */
struct wmEventHandler *WM_event_add_keymap_handler_priority(ListBase *handlers, ListBase *keymap, int priority);

void		WM_event_remove_keymap_handler(ListBase *handlers, ListBase *keymap);

struct wmEventHandler *WM_event_add_ui_handler(struct bContext *C, ListBase *handlers,
			int (*func)(struct bContext *C, struct wmEvent *event, void *userdata),
			void (*remove)(struct bContext *C, void *userdata), void *userdata);
void		WM_event_remove_ui_handler(ListBase *handlers,
			int (*func)(struct bContext *C, struct wmEvent *event, void *userdata),
			void (*remove)(struct bContext *C, void *userdata), void *userdata);

struct wmEventHandler *WM_event_add_modal_handler(struct bContext *C, ListBase *handlers, struct wmOperator *op);
void		WM_event_remove_handlers(struct bContext *C, ListBase *handlers);

void		WM_event_add_mousemove(struct bContext *C);
int			WM_modal_tweak_exit(struct wmEvent *evt, int tweak_event);

void		WM_event_add_notifier(struct bContext *C, unsigned int type, void *data);

void		wm_event_add		(struct wmWindow *win, struct wmEvent *event_to_add); /* XXX only for warning */

			/* at maximum, every timestep seconds it triggers event_type events */
struct wmTimer *WM_event_add_window_timer(struct wmWindow *win, int event_type, double timestep);
void		WM_event_remove_window_timer(struct wmWindow *win, struct wmTimer *timer);
void		WM_event_window_timer_sleep(struct wmWindow *win, struct wmTimer *timer, int dosleep);

		/* operator api, default callbacks */
			/* invoke callback, uses enum property named "type" */
int			WM_menu_invoke			(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
			/* invoke callback, confirm menu + exec */
int			WM_operator_confirm		(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
		/* invoke callback, file selector "filename" unset + exec */
int			WM_operator_filesel		(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
			/* poll callback, context checks */
int			WM_operator_winactive	(struct bContext *C);

		/* operator api */
void		WM_operator_free		(struct wmOperator *op);
wmOperatorType *WM_operatortype_find(const char *idname);
wmOperatorType *WM_operatortype_first(void);
void		WM_operatortype_append	(void (*opfunc)(wmOperatorType*));
void		WM_operatortype_append_ptr	(void (*opfunc)(wmOperatorType*, void *), void *userdata);
int			WM_operatortype_remove(const char *idname);

int			WM_operator_call		(struct bContext *C, struct wmOperator *op);
int         WM_operator_name_call	(struct bContext *C, const char *opstring, int context, struct PointerRNA *properties);
int			WM_operator_call_py(struct bContext *C, struct wmOperatorType *ot, struct PointerRNA *properties, struct ReportList *reports);

void		WM_operator_properties_create(struct PointerRNA *ptr, const char *opstring);
void		WM_operator_properties_free(struct PointerRNA *ptr);

		/* operator as a python command (resultuing string must be free'd) */
char		*WM_operator_pystring(struct wmOperator *op);

			/* default operator callbacks for border/circle/lasso */
int			WM_border_select_invoke	(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int			WM_border_select_modal	(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int			WM_gesture_circle_invoke(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int			WM_gesture_circle_modal(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int			WM_gesture_lasso_invoke(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int			WM_gesture_lasso_modal(struct bContext *C, struct wmOperator *op, struct wmEvent *event);

			/* default operator for arearegions, generates event */
void		WM_OT_tweak_gesture(struct wmOperatorType *ot);

			/* Gesture manager API */
struct wmGesture *WM_gesture_new(struct bContext *C, struct wmEvent *event, int type);
void		WM_gesture_end(struct bContext *C, struct wmGesture *gesture);

			/* radial control operator */
int		WM_radial_control_invoke(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
int		WM_radial_control_modal(struct bContext *C, struct wmOperator *op, struct wmEvent *event);
void		WM_OT_radial_control_partial(struct wmOperatorType *ot);
void		WM_radial_control_string(struct wmOperator *op, char str[], int maxlen);

			/* OpenGL wrappers, mimicking opengl syntax */
void		wmSubWindowSet			(struct wmWindow *win, int swinid);
void		wmSubWindowScissorSet	(struct wmWindow *win, int swinid, struct rcti *srct);

void		wmLoadMatrix		(float mat[][4]);
void		wmGetMatrix			(float mat[][4]);
void		wmMultMatrix		(float mat[][4]);
void		wmGetSingleMatrix	(float mat[][4]);
void		wmScale				(float x, float y, float z);
void		wmLoadIdentity		(void);		/* note: old name clear_view_mat */

void		wmFrustum			(float x1, float x2, float y1, float y2, float n, float f);
void		wmOrtho				(float x1, float x2, float y1, float y2, float n, float f);
void		wmOrtho2			(float x1, float x2, float y1, float y2);

			/* utilities */
void		WM_set_framebuffer_index_color(int index);
int			WM_framebuffer_to_index(unsigned int col);

			/* threaded Jobs Manager */

struct wmJob *WM_jobs_get(struct wmWindowManager *wm, struct wmWindow *win, void *owner);

int			WM_jobs_test(struct wmWindowManager *wm, void *owner);

void		WM_jobs_customdata(struct wmJob *, void *customdata, void (*free)(void *));
void		WM_jobs_timer(struct wmJob *, double timestep, unsigned int note, unsigned int endnote);
void		WM_jobs_callbacks(struct wmJob *, 
							  void (*startjob)(void *, short *, short *),
							  void (*initjob)(void *),
							  void (*update)(void *));

void		WM_jobs_start(struct wmJob *);
void		WM_jobs_stop_all(struct wmWindowManager *wm);

#endif /* WM_API_H */

