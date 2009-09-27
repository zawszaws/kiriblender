#include "BPy_Normal2DF1D.h"

#include "../../../view_map/Functions1D.h"
#include "../../BPy_Convert.h"
#include "../../BPy_IntegrationType.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////

/*---------------  Python API function prototypes for Normal2DF1D instance  -----------*/
static int Normal2DF1D___init__( BPy_Normal2DF1D* self, PyObject *args);

/*-----------------------BPy_Normal2DF1D type definition ------------------------------*/

PyTypeObject Normal2DF1D_Type = {
	PyObject_HEAD_INIT(NULL)
	"Normal2DF1D",                  /* tp_name */
	sizeof(BPy_Normal2DF1D),        /* tp_basicsize */
	0,                              /* tp_itemsize */
	0,                              /* tp_dealloc */
	0,                              /* tp_print */
	0,                              /* tp_getattr */
	0,                              /* tp_setattr */
	0,                              /* tp_reserved */
	0,                              /* tp_repr */
	0,                              /* tp_as_number */
	0,                              /* tp_as_sequence */
	0,                              /* tp_as_mapping */
	0,                              /* tp_hash  */
	0,                              /* tp_call */
	0,                              /* tp_str */
	0,                              /* tp_getattro */
	0,                              /* tp_setattro */
	0,                              /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	"Normal2DF1D objects",          /* tp_doc */
	0,                              /* tp_traverse */
	0,                              /* tp_clear */
	0,                              /* tp_richcompare */
	0,                              /* tp_weaklistoffset */
	0,                              /* tp_iter */
	0,                              /* tp_iternext */
	0,                              /* tp_methods */
	0,                              /* tp_members */
	0,                              /* tp_getset */
	&UnaryFunction1DVec2f_Type,     /* tp_base */
	0,                              /* tp_dict */
	0,                              /* tp_descr_get */
	0,                              /* tp_descr_set */
	0,                              /* tp_dictoffset */
	(initproc)Normal2DF1D___init__, /* tp_init */
	0,                              /* tp_alloc */
	0,                              /* tp_new */
};

//------------------------INSTANCE METHODS ----------------------------------

int Normal2DF1D___init__( BPy_Normal2DF1D* self, PyObject *args)
{
	PyObject *obj = 0;

	if( !PyArg_ParseTuple(args, "|O!", &IntegrationType_Type, &obj) )
		return -1;

	IntegrationType t = ( obj ) ? IntegrationType_from_BPy_IntegrationType(obj) : MEAN;
	self->py_uf1D_vec2f.uf1D_vec2f = new Functions1D::Normal2DF1D(t);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
