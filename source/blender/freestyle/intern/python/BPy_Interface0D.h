#ifndef FREESTYLE_PYTHON_INTERFACE0D_H
#define FREESTYLE_PYTHON_INTERFACE0D_H

#include <Python.h>

#include "../view_map/Interface0D.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject Interface0D_Type;

#define BPy_Interface0D_Check(v)	(  PyObject_IsInstance( (PyObject *) v, (PyObject *) &Interface0D_Type)  )

/*---------------------------Python BPy_Interface0D structure definition----------*/
typedef struct {
	PyObject_HEAD
	Interface0D *if0D;
	int borrowed; /* non-zero if *if0D is a borrowed object */
} BPy_Interface0D;

/*---------------------------Python BPy_Interface0D visible prototypes-----------*/

int Interface0D_Init( PyObject *module );

///////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* FREESTYLE_PYTHON_INTERFACE0D_H */
