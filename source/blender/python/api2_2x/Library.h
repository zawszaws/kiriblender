/* 
 * $Id$
 *
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. The Blender
 * Foundation also sells licenses for use in proprietary software under
 * the Blender License.  See http://www.blender.org/BL/ for information
 * about this.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA	02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * This is a new part of Blender.
 *
 * Contributor(s): Ken Hughes
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
*/

#ifndef EXPP_LIBRARY_H
#define EXPP_LIBRARY_H

#include <Python.h>
#include "DNA_scene_types.h"
#include "BLI_linklist.h"

#include "blendef.h"

/*****************************************************************************/
/* Python V24_BPy_Library structure definition:        */
/*****************************************************************************/
typedef struct {
	PyObject_HEAD 
	char filename[FILE_MAXDIR + FILE_MAXFILE];
} V24_BPy_Library;

typedef struct {
	PyObject_HEAD 
	LinkNode *iter;
	int type;
	char filename[FILE_MAXDIR + FILE_MAXFILE];
	char *name;
	enum {
		OBJECT_IS_LINK,
		OBJECT_IS_APPEND,
		OTHER
	} kind;
} V24_BPy_LibraryData;

extern PyTypeObject V24_Library_Type;
extern PyTypeObject V24_LibraryData_Type;

#define BPy_LibraryData_Check(v)       ((v)->ob_type == &V24_LibraryData_Type)
#define BPy_Library_Check(v)       ((v)->ob_type == &V24_Library_Type)

/*****************************************************************************/
/* Module Blender.Library - public functions	 */
/*****************************************************************************/
PyObject *V24_Library_Init( void );
PyObject *V24_oldLibrary_Init( void );

PyObject *V24_LibraryData_importLibData( V24_BPy_LibraryData *self, char *name,
		int mode, Scene *scene );

#endif				/* EXPP_LIBRARY_H */
