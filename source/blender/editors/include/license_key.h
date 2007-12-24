/**
 * $Id: license_key.h 229 2002-12-27 13:11:01Z mein $
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
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
 */
#ifndef LICENCEKEY_H
#define LICENCEKEY_H

#define I_AM_PUBLISHER temp_val2
#define LICENSE_KEY_VALID temp_val
#define SHOW_LICENSE_KEY rotop

extern int LICENSE_KEY_VALID;
extern int I_AM_PUBLISHER;

extern char * license_key_name;
extern void loadKeyboard(char * name);
extern void checkhome(void);
extern void SHOW_LICENSE_KEY(void);

#define LICENSE_CHECK_0 (0==0)

// Stuff from the Python files from Strubi

typedef int (*Fptr)(void *);

extern Fptr g_functab[];
extern Fptr g_ptrtab[];

// TODO: From here on, this should be a generated header file...

// change all KEY_FUNC values 
// if you change PYKEY_TABLEN or PYKEY_SEED
// see below

#define PYKEY_TABLEN 21 // don't change this unless needed. Other values
                        // may yield bad random orders

#define PYKEY_SEED {26,8,1972}

// these values are generated by $HOME/develop/intern/keymaker/makeseed.py
// from the above seed value. 

// DO NOT EDIT THESE VALUES BY HAND!

#define KEY_FUNC1 12
#define KEY_FUNC2 8
#define KEY_FUNC3 1
#define KEY_FUNC4 16
#define KEY_FUNC5 20
#define KEY_FUNC6 18
#define KEY_FUNC7 13
#define KEY_FUNC8 6
#define KEY_FUNC9 9
#define KEY_FUNC10 7
#define KEY_FUNC11 14
#define KEY_FUNC12 0
#define KEY_FUNC13 5
#define KEY_FUNC14 10
#define KEY_FUNC15 19
#define KEY_FUNC16 2
#define KEY_FUNC17 11
#define KEY_FUNC18 3
#define KEY_FUNC19 17
#define KEY_FUNC20 15
#define KEY_FUNC21 4

#endif

