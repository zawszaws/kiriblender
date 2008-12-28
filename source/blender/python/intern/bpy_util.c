/**
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
 * Contributor(s): Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "bpy_util.h"
#include "BLI_dynstr.h"
#include "MEM_guardedalloc.h"

PyObject *BPY_flag_to_list(struct BPY_flag_def *flagdef, int flag)
{
	PyObject *list = PyList_New(0);
	
	PyObject *item;
	BPY_flag_def *fd;

	fd= flagdef;
	while(fd->name) {
		if (fd->flag & flag) {
			item = PyUnicode_FromString(fd->name);
			PyList_Append(list, item);
			Py_DECREF(item);
		}
		fd++;
	}
	
	return list;

}

static char *bpy_flag_error_str(BPY_flag_def *flagdef)
{
	BPY_flag_def *fd= flagdef;
	DynStr *dynstr= BLI_dynstr_new();
	char *cstring;

	BLI_dynstr_append(dynstr, "Error converting a sequence of strings into a flag.\n\tExpected only these strings...\n\t");

	while(fd->name) {
		BLI_dynstr_appendf(dynstr, fd!=flagdef?", '%s'":"'%s'", fd->name);
		fd++;
	}
	
	cstring = BLI_dynstr_get_cstring(dynstr);
	BLI_dynstr_free(dynstr);
	return cstring;
}

int BPY_flag_from_seq(BPY_flag_def *flagdef, PyObject *seq, int *flag)
{
	int i, error_val= 0;
	char *cstring;
	PyObject *item;
	BPY_flag_def *fd;

	if (PySequence_Check(seq)) {
		i= PySequence_Length(seq);

		while(i--) {
			item = PySequence_ITEM(seq, i);
			cstring= _PyUnicode_AsString(item);
			if(cstring) {
				fd= flagdef;
				while(fd->name) {
					if (strcmp(cstring, fd->name) == 0)
						(*flag) |= fd->flag;
					fd++;
				}
				if (fd==NULL) { /* could not find a match */
					error_val= 1;
				}
			} else {
				error_val= 1;
			}
			Py_DECREF(item);
		}
	}
	else {
		error_val= 1;
	}

	if (error_val) {
		char *buf = bpy_flag_error_str(flagdef);
		PyErr_SetString(PyExc_AttributeError, buf);
		MEM_freeN(buf);
		return -1; /* error value */
	}

	return 0; /* ok */
}

/* for debugging */
void PyObSpit(char *name, PyObject *var) {
	fprintf(stderr, "<%s> : ", name);
	if (var==NULL) {
		fprintf(stderr, "<NIL>");
	}
	else {
		PyObject_Print(var, stderr, 0);
	}
	fprintf(stderr, "\n");
}
