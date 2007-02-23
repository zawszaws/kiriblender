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
 * Contributor(s): Willian P. Germano, Johnny Matthews, Ken Hughes
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
*/

#include "Camera.h" /*This must come first */

#include "BKE_main.h"
#include "BKE_global.h"
#include "BKE_object.h"
#include "BKE_library.h"
#include "BLI_blenlib.h"
#include "BSE_editipo.h"
#include "BIF_space.h"
#include "mydevice.h"
#include "gen_utils.h"
#include "Ipo.h"


#define IPOKEY_LENS 0
#define IPOKEY_CLIPPING 1



enum cam_consts {
	EXPP_CAM_ATTR_LENS = 0,
	EXPP_CAM_ATTR_DOFDIST,
	EXPP_CAM_ATTR_CLIPEND,
	EXPP_CAM_ATTR_CLIPSTART,
	EXPP_CAM_ATTR_SCALE,
	EXPP_CAM_ATTR_DRAWSIZE,
	EXPP_CAM_ATTR_SHIFTX,
	EXPP_CAM_ATTR_SHIFTY,
	EXPP_CAM_ATTR_ALPHA,
};

/*****************************************************************************/
/* Python API function prototypes for the Camera module.                     */
/*****************************************************************************/
static PyObject *M_Camera_New( PyObject * self, PyObject * args,
			       PyObject * keywords );
static PyObject *M_Camera_Get( PyObject * self, PyObject * args );

/*****************************************************************************/
/* The following string definitions are used for documentation strings.      */
/* In Python these will be written to the console when doing a               */
/* Blender.Camera.__doc__                                                    */
/*****************************************************************************/
static char M_Camera_doc[] = "The Blender Camera module\n\
\n\
This module provides access to **Camera Data** objects in Blender\n\
\n\
Example::\n\
\n\
  from Blender import Camera, Object, Scene\n\
  c = Camera.New('ortho')      # create new ortho camera data\n\
  c.scale = 6.0                # set scale value\n\
  scn = Scene.GetCurrent()     # get current Scene\n\
  ob = scn.objects.new(c)      # Make an object from this data in the scene\n\
  cur.setCurrentCamera(ob)     # make this camera the active";

static char M_Camera_New_doc[] =
	"Camera.New (type = 'persp', name = 'CamData'):\n\
        Return a new Camera Data object with the given type and name.";

static char M_Camera_Get_doc[] = "Camera.Get (name = None):\n\
        Return the camera data with the given 'name', None if not found, or\n\
        Return a list with all Camera Data objects in the current scene,\n\
        if no argument was given.";

/*****************************************************************************/
/* Python method structure definition for Blender.Camera module:             */
/*****************************************************************************/
struct PyMethodDef M_Camera_methods[] = {
	{"New", ( PyCFunction ) M_Camera_New, METH_VARARGS | METH_KEYWORDS,
	 M_Camera_New_doc},
	{"Get", M_Camera_Get, METH_VARARGS, M_Camera_Get_doc},
	{"get", M_Camera_Get, METH_VARARGS, M_Camera_Get_doc},
	{NULL, NULL, 0, NULL}
};

/*****************************************************************************/
/* Python BPy_Camera methods declarations:                                   */
/*****************************************************************************/
static PyObject *Camera_oldgetIpo( BPy_Camera * self );
static PyObject *Camera_oldgetName( BPy_Camera * self );
static PyObject *Camera_oldgetType( BPy_Camera * self );
static PyObject *Camera_oldgetMode( BPy_Camera * self );
static PyObject *Camera_oldgetLens( BPy_Camera * self );
static PyObject *Camera_oldgetClipStart( BPy_Camera * self );
static PyObject *Camera_oldgetClipEnd( BPy_Camera * self );
static PyObject *Camera_oldgetDrawSize( BPy_Camera * self );
static PyObject *Camera_oldgetScale( BPy_Camera * self );
static PyObject *Camera_oldsetIpo( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetName( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetType( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetMode( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetLens( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetClipStart( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetClipEnd( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetDrawSize( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldsetScale( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldgetScriptLinks( BPy_Camera * self, PyObject * args );
static PyObject *Camera_addScriptLink( BPy_Camera * self, PyObject * args );
static PyObject *Camera_oldclearIpo( BPy_Camera * self );
static PyObject *Camera_clearScriptLinks( BPy_Camera * self, PyObject * args );
static PyObject *Camera_insertIpoKey( BPy_Camera * self, PyObject * args );
static PyObject *Camera_copy( BPy_Camera * self );

Camera *GetCameraByName( char *name );


/*****************************************************************************/
/* Python BPy_Camera methods table:                                          */
/*****************************************************************************/
static PyMethodDef BPy_Camera_methods[] = {
	/* name, method, flags, doc */
	{"getIpo", ( PyCFunction ) Camera_oldgetIpo, METH_NOARGS,
	 "() - Return Camera Data Ipo"},
	{"getName", ( PyCFunction ) Camera_oldgetName, METH_NOARGS,
	 "() - Return Camera Data name"},
	{"getType", ( PyCFunction ) Camera_oldgetType, METH_NOARGS,
	 "() - Return Camera type - 'persp':0, 'ortho':1"},
	{"getMode", ( PyCFunction ) Camera_oldgetMode, METH_NOARGS,
	 "() - Return Camera mode flags (or'ed value) -\n"
	 "     'showLimits':1, 'showMist':2"},
	{"getLens", ( PyCFunction ) Camera_oldgetLens, METH_NOARGS,
	 "() - Return *perspective* Camera lens value"},
	{"getScale", ( PyCFunction ) Camera_oldgetScale, METH_NOARGS,
	 "() - Return *ortho* Camera scale value"},
	{"getClipStart", ( PyCFunction ) Camera_oldgetClipStart, METH_NOARGS,
	 "() - Return Camera clip start value"},
	{"getClipEnd", ( PyCFunction ) Camera_oldgetClipEnd, METH_NOARGS,
	 "() - Return Camera clip end value"},
	{"getDrawSize", ( PyCFunction ) Camera_oldgetDrawSize, METH_NOARGS,
	 "() - Return Camera draw size value"},
	{"setIpo", ( PyCFunction ) Camera_oldsetIpo, METH_VARARGS,
	 "(Blender Ipo) - Set Camera Ipo"},
	{"clearIpo", ( PyCFunction ) Camera_oldclearIpo, METH_NOARGS,
	 "() - Unlink Ipo from this Camera."},
	 {"insertIpoKey", ( PyCFunction ) Camera_insertIpoKey, METH_VARARGS,
	 "( Camera IPO type ) - Inserts a key into IPO"},
	{"setName", ( PyCFunction ) Camera_oldsetName, METH_VARARGS,
	 "(s) - Set Camera Data name"},
	{"setType", ( PyCFunction ) Camera_oldsetType, METH_VARARGS,
	 "(s) - Set Camera type, which can be 'persp' or 'ortho'"},
	{"setMode", ( PyCFunction ) Camera_oldsetMode, METH_VARARGS,
	 "(<s<,s>>) - Set Camera mode flag(s): 'showLimits' and 'showMist'"},
	{"setLens", ( PyCFunction ) Camera_oldsetLens, METH_VARARGS,
	 "(f) - Set *perpective* Camera lens value"},
	{"setScale", ( PyCFunction ) Camera_oldsetScale, METH_VARARGS,
	 "(f) - Set *ortho* Camera scale value"},
	{"setClipStart", ( PyCFunction ) Camera_oldsetClipStart, METH_VARARGS,
	 "(f) - Set Camera clip start value"},
	{"setClipEnd", ( PyCFunction ) Camera_oldsetClipEnd, METH_VARARGS,
	 "(f) - Set Camera clip end value"},
	{"setDrawSize", ( PyCFunction ) Camera_oldsetDrawSize, METH_VARARGS,
	 "(f) - Set Camera draw size value"},
	{"getScriptLinks", ( PyCFunction ) Camera_oldgetScriptLinks, METH_VARARGS,
	 "(eventname) - Get a list of this camera's scriptlinks (Text names) "
	 "of the given type\n"
	 "(eventname) - string: FrameChanged, Redraw or Render."},
	{"addScriptLink", ( PyCFunction ) Camera_addScriptLink, METH_VARARGS,
	 "(text, evt) - Add a new camera scriptlink.\n"
	 "(text) - string: an existing Blender Text name;\n"
	 "(evt) string: FrameChanged, Redraw or Render."},
	{"clearScriptLinks", ( PyCFunction ) Camera_clearScriptLinks,
	 METH_NOARGS,
	 "() - Delete all scriptlinks from this camera.\n"
	 "([s1<,s2,s3...>]) - Delete specified scriptlinks from this camera."},
	{"__copy__", ( PyCFunction ) Camera_copy, METH_NOARGS,
	 "() - Return a copy of the camera."},
	{NULL, NULL, 0, NULL}
};

/*****************************************************************************/
/* Python Camera_Type callback function prototypes:                          */
/*****************************************************************************/
static void Camera_dealloc( BPy_Camera * self );
//static int Camera_setAttr( BPy_Camera * self, char *name, PyObject * v );
static int Camera_compare( BPy_Camera * a, BPy_Camera * b );
//static PyObject *Camera_getAttr( BPy_Camera * self, char *name );
static PyObject *Camera_repr( BPy_Camera * self );


//~ /*****************************************************************************/
//~ /* Python Camera_Type structure definition:	                             */
//~ /*****************************************************************************/
//~ PyTypeObject Camera_Type = {
	//~ PyObject_HEAD_INIT( NULL ) /* required macro */ 
	//~ NULL,	/* ob_size */
	//~ "Blender Camera",	/* tp_name */
	//~ sizeof( BPy_Camera ),	/* tp_basicsize */
	//~ NULL,			/* tp_itemsize */
	//~ /* methods */
	//~ ( destructor ) Camera_dealloc,	/* tp_dealloc */
	//~ NULL,			/* tp_print */
	//~ NULL,	/* tp_getattr */
	//~ NULL,	/* tp_setattr */
	//~ ( cmpfunc ) Camera_compare,	/* tp_compare */
	//~ ( reprfunc ) Camera_repr,	/* tp_repr */
	//~ NULL,			/* tp_as_number */
	//~ NULL,			/* tp_as_sequence */
	//~ NULL,			/* tp_as_mapping */
	//~ NULL,			/* tp_as_hash */
	//~ 0, 0, 0, 0, 0, 0,
	//~ 0,			/* tp_doc */
	//~ 0, 0, 0, 0, 0, 0,
	//~ BPy_Camera_methods,	/* tp_methods */
	//~ 0,			/* tp_members */
	//~ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//~ };




















static PyObject *M_Camera_New( PyObject * self, PyObject * args,
			       PyObject * kwords )
{
	char *type_str = "persp";	/* "persp" is type 0, "ortho" is type 1 */
	char *name_str = "CamData";
	static char *kwlist[] = { "type_str", "name_str", NULL };
	PyObject *pycam;	/* for Camera Data object wrapper in Python */
	Camera *blcam;		/* for actual Camera Data we create in Blender */
	char buf[21];

	/* Parse the arguments passed in by the Python interpreter */
	if( !PyArg_ParseTupleAndKeywords( args, kwords, "|ss", kwlist,
					  &type_str, &name_str ) )
		/* We expected string(s) (or nothing) as argument, but we didn't get that. */
		return EXPP_ReturnPyObjError( PyExc_AttributeError,
					      "expected zero, one or two strings as arguments" );

	blcam = add_camera(  );	/* first create the Camera Data in Blender */

	if( blcam )		/* now create the wrapper obj in Python */
		pycam = Camera_CreatePyObject( blcam );
	else
		return EXPP_ReturnPyObjError( PyExc_RuntimeError,
					      "couldn't create Camera Data in Blender" );

	/* let's return user count to zero, because ... */
	blcam->id.us = 0;	/* ... add_camera() incref'ed it */
	/* XXX XXX Do this in other modules, too */

	if( pycam == NULL )
		return EXPP_ReturnPyObjError( PyExc_MemoryError,
					      "couldn't create Camera PyObject" );

	if( strcmp( type_str, "persp" ) == 0 )
		/* default, no need to set, so */
		/*blcam->type = (short)EXPP_CAM_TYPE_PERSP */
		;
	/* we comment this line */
	else if( strcmp( type_str, "ortho" ) == 0 )
		blcam->type = ( short ) EXPP_CAM_TYPE_ORTHO;
	else
		return EXPP_ReturnPyObjError( PyExc_AttributeError,
					      "unknown camera type" );

	if( strcmp( name_str, "CamData" ) == 0 )
		return pycam;
	else {			/* user gave us a name for the camera, use it */
		PyOS_snprintf( buf, sizeof( buf ), "%s", name_str );
		rename_id( &blcam->id, buf );	/* proper way in Blender */
	}

	return pycam;
}

static PyObject *M_Camera_Get( PyObject * self, PyObject * args )
{
	char *name = NULL;
	Camera *cam_iter;

	if( !PyArg_ParseTuple( args, "|s", &name ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected string argument (or nothing)" );

	cam_iter = G.main->camera.first;

	if( name ) {		/* (name) - Search camera by name */

		PyObject *wanted_cam = NULL;

		while( cam_iter && !wanted_cam ) {

			if( strcmp( name, cam_iter->id.name + 2 ) == 0 ) {
				wanted_cam = Camera_CreatePyObject( cam_iter );
				break;
			}

			cam_iter = cam_iter->id.next;
		}

		if( !wanted_cam ) {	/* Requested camera doesn't exist */
			char error_msg[64];
			PyOS_snprintf( error_msg, sizeof( error_msg ),
				       "Camera \"%s\" not found", name );
			return EXPP_ReturnPyObjError( PyExc_NameError,
						      error_msg );
		}

		return wanted_cam;
	}

	else {			/* () - return a list of wrappers for all cameras in the scene */
		int index = 0;
		PyObject *cam_pylist, *pyobj;

		cam_pylist =
			PyList_New( BLI_countlist( &( G.main->camera ) ) );

		if( !cam_pylist )
			return EXPP_ReturnPyObjError( PyExc_MemoryError,
						      "couldn't create PyList" );

		while( cam_iter ) {
			pyobj = Camera_CreatePyObject( cam_iter );

			if( !pyobj )
				return EXPP_ReturnPyObjError
					( PyExc_MemoryError,
					  "couldn't create Camera PyObject" );

			PyList_SET_ITEM( cam_pylist, index, pyobj );

			cam_iter = cam_iter->id.next;
			index++;
		}

		return cam_pylist;
	}
}

PyObject *Camera_Init( void )
{
	PyObject *submodule;

	if( PyType_Ready( &Camera_Type ) < 0 )
		return NULL;
	
	submodule = Py_InitModule3( "Blender.Camera",
				    M_Camera_methods, M_Camera_doc );

	PyModule_AddIntConstant( submodule, "LENS",     IPOKEY_LENS );
	PyModule_AddIntConstant( submodule, "CLIPPING", IPOKEY_CLIPPING );

	return submodule;
}




















/* Three Python Camera_Type helper functions needed by the Object module: */

PyObject *Camera_CreatePyObject( Camera * cam )
{
	BPy_Camera *pycam;

	pycam = ( BPy_Camera * ) PyObject_NEW( BPy_Camera, &Camera_Type );

	if( !pycam )
		return EXPP_ReturnPyObjError( PyExc_MemoryError,
					      "couldn't create BPy_Camera PyObject" );

	pycam->camera = cam;

	return ( PyObject * ) pycam;
}

int Camera_CheckPyObject( PyObject * pyobj )
{
	return ( pyobj->ob_type == &Camera_Type );
}

Camera *Camera_FromPyObject( PyObject * pyobj )
{
	return ( ( BPy_Camera * ) pyobj )->camera;
}

/*****************************************************************************/
/* Description: Returns the object with the name specified by the argument  */
/*	name. Note that the calling function has to remove the first */
/*	two characters of the object name. These two characters		 */
/*	specify the type of the object (OB, ME, WO, ...)		 */
/*	The function will return NULL when no object with the given  */
/*	name is found.							    */
/*****************************************************************************/
Camera *GetCameraByName( char *name )
{
	Camera *cam_iter;

	cam_iter = G.main->camera.first;
	while( cam_iter ) {
		if( StringEqual( name, GetIdName( &( cam_iter->id ) ) ) ) {
			return ( cam_iter );
		}
		cam_iter = cam_iter->id.next;
	}

	/* There is no camera with the given name */
	return ( NULL );
}

/*****************************************************************************/
/* Python BPy_Camera methods:                                               */
/*****************************************************************************/

static PyObject *Camera_oldgetIpo( BPy_Camera * self )
{
	struct Ipo *ipo = self->camera->ipo;

	if( !ipo )
		Py_RETURN_NONE;

	return Ipo_CreatePyObject( ipo );
}


static PyObject *Camera_oldgetName( BPy_Camera * self )
{

	PyObject *attr = PyString_FromString( self->camera->id.name + 2 );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.name attribute" );
}

static PyObject *Camera_oldgetType( BPy_Camera * self )
{
	PyObject *attr = PyInt_FromLong( self->camera->type );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.type attribute" );
}

static PyObject *Camera_oldgetMode( BPy_Camera * self )
{
	PyObject *attr = PyInt_FromLong( self->camera->flag );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.Mode attribute" );
}

static PyObject *Camera_oldgetLens( BPy_Camera * self )
{
	PyObject *attr = PyFloat_FromDouble( self->camera->lens );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.lens attribute" );
}

static PyObject *Camera_oldgetScale( BPy_Camera * self )
{
	PyObject *attr = PyFloat_FromDouble( self->camera->ortho_scale );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.scale attribute" );
}

static PyObject *Camera_oldgetClipStart( BPy_Camera * self )
{
	PyObject *attr = PyFloat_FromDouble( self->camera->clipsta );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.clipStart attribute" );
}

static PyObject *Camera_oldgetClipEnd( BPy_Camera * self )
{
	PyObject *attr = PyFloat_FromDouble( self->camera->clipend );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.clipEnd attribute" );
}

static PyObject *Camera_oldgetDrawSize( BPy_Camera * self )
{
	PyObject *attr = PyFloat_FromDouble( self->camera->drawsize );

	if( attr )
		return attr;

	return EXPP_ReturnPyObjError( PyExc_RuntimeError,
				      "couldn't get Camera.drawSize attribute" );
}



static PyObject *Camera_oldsetIpo( BPy_Camera * self, PyObject * args )
{
	PyObject *pyipo = 0;
	Ipo *ipo = NULL;
	Ipo *oldipo;

	if( !PyArg_ParseTuple( args, "O!", &Ipo_Type, &pyipo ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected Ipo as argument" );

	ipo = Ipo_FromPyObject( pyipo );

	if( !ipo )
		return EXPP_ReturnPyObjError( PyExc_RuntimeError,
					      "null ipo!" );

	if( ipo->blocktype != ID_CA )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "this ipo is not a camera data ipo" );

	oldipo = self->camera->ipo;
	if( oldipo ) {
		ID *id = &oldipo->id;
		if( id->us > 0 )
			id->us--;
	}

	id_us_plus(&ipo->id);

	self->camera->ipo = ipo;

	Py_RETURN_NONE;
}

static PyObject *Camera_oldclearIpo( BPy_Camera * self )
{
	Camera *cam = self->camera;
	Ipo *ipo = ( Ipo * ) cam->ipo;

	if( ipo ) {
		ID *id = &ipo->id;
		if( id->us > 0 )
			id->us--;
		cam->ipo = NULL;

		return EXPP_incr_ret_True();
	}

	return EXPP_incr_ret_False(); /* no ipo found */
}

static PyObject *Camera_oldsetName( BPy_Camera * self, PyObject * args )
{
	char *name;

	if( !PyArg_ParseTuple( args, "s", &name ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected string argument" );

	rename_id( &self->camera->id, name );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetType( BPy_Camera * self, PyObject * args )
{
	char *type;

	if( !PyArg_ParseTuple( args, "s", &type ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected string argument" );

	if( strcmp( type, "persp" ) == 0 )
		self->camera->type = ( short ) EXPP_CAM_TYPE_PERSP;
	else if( strcmp( type, "ortho" ) == 0 )
		self->camera->type = ( short ) EXPP_CAM_TYPE_ORTHO;
	else
		return EXPP_ReturnPyObjError( PyExc_AttributeError,
					      "unknown camera type" );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetMode( BPy_Camera * self, PyObject * args )
{
	char *mode_str1 = NULL, *mode_str2 = NULL;
	short flag = 0;

	if( !PyArg_ParseTuple( args, "|ss", &mode_str1, &mode_str2 ) )
		return EXPP_ReturnPyObjError( PyExc_AttributeError,
					      "expected one or two strings as arguments" );

	if( mode_str1 != NULL ) {
		if( strcmp( mode_str1, "showLimits" ) == 0 )
			flag |= ( short ) EXPP_CAM_MODE_SHOWLIMITS;
		else if( strcmp( mode_str1, "showMist" ) == 0 )
			flag |= ( short ) EXPP_CAM_MODE_SHOWMIST;
		else
			return EXPP_ReturnPyObjError( PyExc_AttributeError,
						      "first argument is an unknown camera flag" );

		if( mode_str2 != NULL ) {
			if( strcmp( mode_str2, "showLimits" ) == 0 )
				flag |= ( short ) EXPP_CAM_MODE_SHOWLIMITS;
			else if( strcmp( mode_str2, "showMist" ) == 0 )
				flag |= ( short ) EXPP_CAM_MODE_SHOWMIST;
			else
				return EXPP_ReturnPyObjError
					( PyExc_AttributeError,
					  "second argument is an unknown camera flag" );
		}
	}

	self->camera->flag = flag;

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetLens( BPy_Camera * self, PyObject * args )
{
	float value;

	if( !PyArg_ParseTuple( args, "f", &value ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected float argument" );

	self->camera->lens = EXPP_ClampFloat( value,
					      EXPP_CAM_LENS_MIN,
					      EXPP_CAM_LENS_MAX );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetScale( BPy_Camera * self, PyObject * args )
{
	float value;

	if( !PyArg_ParseTuple( args, "f", &value ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected float argument" );

	self->camera->ortho_scale = EXPP_ClampFloat( value,
					      EXPP_CAM_SCALE_MIN,
					      EXPP_CAM_SCALE_MAX );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetClipStart( BPy_Camera * self, PyObject * args )
{
	float value;

	if( !PyArg_ParseTuple( args, "f", &value ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected float argument" );

	self->camera->clipsta = EXPP_ClampFloat( value,
						 EXPP_CAM_CLIPSTART_MIN,
						 EXPP_CAM_CLIPSTART_MAX );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetClipEnd( BPy_Camera * self, PyObject * args )
{
	float value;

	if( !PyArg_ParseTuple( args, "f", &value ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected float argument" );

	self->camera->clipend = EXPP_ClampFloat( value,
						 EXPP_CAM_CLIPEND_MIN,
						 EXPP_CAM_CLIPEND_MAX );

	Py_RETURN_NONE;
}

static PyObject *Camera_oldsetDrawSize( BPy_Camera * self, PyObject * args )
{
	float value;

	if( !PyArg_ParseTuple( args, "f", &value ) )
		return EXPP_ReturnPyObjError( PyExc_TypeError,
					      "expected a float number as argument" );

	self->camera->drawsize = EXPP_ClampFloat( value,
						  EXPP_CAM_DRAWSIZE_MIN,
						  EXPP_CAM_DRAWSIZE_MAX );

	Py_RETURN_NONE;
}

/* cam.addScriptLink */
static PyObject *Camera_addScriptLink( BPy_Camera * self, PyObject * args )
{
	Camera *cam = self->camera;
	ScriptLink *slink = NULL;

	slink = &( cam )->scriptlink;

	return EXPP_addScriptLink( slink, args, 0 );
}

/* cam.clearScriptLinks */
static PyObject *Camera_clearScriptLinks( BPy_Camera * self, PyObject * args )
{
	Camera *cam = self->camera;
	ScriptLink *slink = NULL;

	slink = &( cam )->scriptlink;

	return EXPP_clearScriptLinks( slink, args );
}

/* cam.getScriptLinks */
static PyObject *Camera_oldgetScriptLinks( BPy_Camera * self, PyObject * args )
{
	Camera *cam = self->camera;
	ScriptLink *slink = NULL;
	PyObject *ret = NULL;

	slink = &( cam )->scriptlink;

	ret = EXPP_getScriptLinks( slink, args, 0 );

	if( ret )
		return ret;
	else
		return NULL;
}

/* cam.__copy__ */
static PyObject *Camera_copy( BPy_Camera * self )
{
	PyObject *pycam;	/* for Camera Data object wrapper in Python */
	Camera *blcam;		/* for actual Camera Data we create in Blender */

	blcam = copy_camera( self->camera );	/* first create the Camera Data in Blender */

	if( blcam )		/* now create the wrapper obj in Python */
		pycam = Camera_CreatePyObject( blcam );
	else
		return EXPP_ReturnPyObjError( PyExc_RuntimeError,
					      "couldn't create Camera Data in Blender" );

	/* let's return user count to zero, because ... */
	blcam->id.us = 0;	/* ... copy_camera() incref'ed it */
	/* XXX XXX Do this in other modules, too */

	if( pycam == NULL )
		return EXPP_ReturnPyObjError( PyExc_MemoryError,
					      "couldn't create Camera PyObject" );

	return pycam;
}

static void Camera_dealloc( BPy_Camera * self )
{
	PyObject_DEL( self );
}

static PyObject *Camera_getName( BPy_Camera * self )
{
	return PyString_FromString( self->camera->id.name + 2 );
}

static int Camera_setName( BPy_Camera * self, PyObject * value )
{
	char *name = NULL;
	
	name = PyString_AsString ( value );
	if( !name )
		return EXPP_ReturnIntError( PyExc_TypeError,
					      "expected string argument" );
	
	rename_id( &self->camera->id, name);
	return 0;
}

static PyObject *Camera_getLib( BPy_Camera * self )
{
	return EXPP_GetIdLib((ID *)self->camera);
}

static PyObject *Camera_getUsers( BPy_Camera * self )
{
	return PyInt_FromLong( self->camera->id.us );
}


static PyObject *Camera_getFakeUser( BPy_Camera * self )
{
	if (self->camera->id.flag & LIB_FAKEUSER)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}

static int Camera_setFakeUser( BPy_Camera * self, PyObject * value )
{
	return SetIdFakeUser(&self->camera->id, value);
}


static PyObject *Camera_getType( BPy_Camera * self )
{
	if (self->camera->type == EXPP_CAM_TYPE_PERSP)
		return PyString_FromString("persp");
	else /* must be EXPP_CAM_TYPE_ORTHO */
		return PyString_FromString("ortho");
}

static int Camera_setType( BPy_Camera * self, PyObject * value )
{
	char *type = NULL;
	type = PyString_AsString(value);
	
	if (!type)
		return EXPP_ReturnIntError( PyExc_ValueError,
					      "expected a string" );
	if (strcmp("persp", type)==0) {
		self->camera->type = EXPP_CAM_TYPE_PERSP;
		return 0;
	} else if (strcmp("ortho", type)==0) {
		self->camera->type = EXPP_CAM_TYPE_ORTHO;
		return 0;
	}
	
	return EXPP_ReturnIntError( PyExc_ValueError,
		"expected a string \"ortho\" or \"persp\"" );
}



static PyObject *Camera_getMode( BPy_Camera * self )
{
	return PyInt_FromLong(self->camera->flag);
}

static int Camera_setMode( BPy_Camera * self, PyObject * value )
{
	unsigned int flag = 0;
	
	if( !PyInt_CheckExact( value ) )
		return EXPP_ReturnIntError( PyExc_TypeError,
			"expected an integer (bitmask) as argument" );
	
	flag = ( unsigned int )PyInt_AS_LONG( value );
	
	self->camera->flag = flag;
	return 0;
}

static PyObject *Camera_getIpo( BPy_Camera * self )
{
	struct Ipo *ipo = self->camera->ipo;

	if( ipo )
		return Ipo_CreatePyObject( ipo );
	Py_RETURN_NONE;
}

static int Camera_setIpo( BPy_Camera * self, PyObject * value )
{
	Ipo *ipo = NULL;
	Ipo *oldipo = self->camera->ipo;
	ID *id;

	/* if parameter is not None, check for valid Ipo */

	if ( value != Py_None ) {
		if ( !Ipo_CheckPyObject( value ) )
			return EXPP_ReturnIntError( PyExc_TypeError,
					"expected an Ipo object" );

		ipo = Ipo_FromPyObject( value );

		if( !ipo )
			return EXPP_ReturnIntError( PyExc_RuntimeError,
					"null ipo!" );

		if( ipo->blocktype != ID_CA )
			return EXPP_ReturnIntError( PyExc_TypeError,
					"Ipo is not a camera data Ipo" );
	}

	/* if already linked to Ipo, delete link */

	if ( oldipo ) {
		id = &oldipo->id;
		if( id->us > 0 )
			id->us--;
	}

	/* assign new Ipo and increment user count, or set to NULL if deleting */

	self->camera->ipo = ipo;
	if ( ipo )
		id_us_plus(&ipo->id);

	return 0;
}

/*
 * get floating point attributes
 */

static PyObject *getFloatAttr( BPy_Camera *self, void *type )
{
	float param;
	struct Camera *cam= self->camera;

	switch( (int)type ) {
	case EXPP_CAM_ATTR_LENS: 
		param = cam->lens;
		break;
	case EXPP_CAM_ATTR_DOFDIST: 
		param = cam->YF_dofdist;
		break;
	case EXPP_CAM_ATTR_CLIPSTART: 
		param = cam->clipsta;
		break;
	case EXPP_CAM_ATTR_CLIPEND: 
		param = cam->clipend;
		break;
	case EXPP_CAM_ATTR_DRAWSIZE: 
		param = cam->drawsize;
		break;
	case EXPP_CAM_ATTR_SCALE: 
		param = cam->ortho_scale;
		break;
	case EXPP_CAM_ATTR_ALPHA: 
		param = cam->passepartalpha;
		break;
	case EXPP_CAM_ATTR_SHIFTX: 
		param = cam->shiftx;
		break;
	case EXPP_CAM_ATTR_SHIFTY:
		param = cam->shifty;
		break;
	
	default:
		return EXPP_ReturnPyObjError( PyExc_RuntimeError, 
				"undefined type in getFloatAttr" );
	}

	return PyFloat_FromDouble( param );
}



/*
 * set floating point attributes which require clamping
 */

static int setFloatAttrClamp( BPy_Camera *self, PyObject *value, void *type )
{
	float *param;
	struct Camera *cam = self->camera;
	float min, max;


	switch( (int)type ) {
	case EXPP_CAM_ATTR_LENS:
		min = 1.0;
		max = 250.0;
		param = &cam->lens;
		break;
	case EXPP_CAM_ATTR_DOFDIST:
		min = 0.0;
		max = 5000.0;
		param = &cam->YF_dofdist;
		break;
	case EXPP_CAM_ATTR_CLIPSTART:
		min = 0.0;
		max = 100.0;
		param = &cam->clipsta;
		break;
	case EXPP_CAM_ATTR_CLIPEND:
		min = 1.0;
		max = 5000.0;
		param = &cam->clipend;
		break;
	case EXPP_CAM_ATTR_DRAWSIZE:
		min = 0.1;
		max = 10.0;
		param = &cam->drawsize;
		break;
	case EXPP_CAM_ATTR_SCALE:
		min = 0.01;
		max = 1000.0;
		param = &cam->ortho_scale;
		break;
	case EXPP_CAM_ATTR_ALPHA:
		min = 0.0;
		max = 1.0;
		param = &cam->passepartalpha;
		break;
	case EXPP_CAM_ATTR_SHIFTX:
		min = -2.0;
		max =  2.0;
		param = &cam->shiftx;
		break;
	case EXPP_CAM_ATTR_SHIFTY:
		min = -2.0;
		max =  2.0;
		param = &cam->shifty;
		break;
	
	default:
		return EXPP_ReturnIntError( PyExc_RuntimeError,
				"undefined type in setFloatAttrClamp" );
	}

	return EXPP_setFloatClamped( value, param, min, max );
}


/*
 * get floating point attributes
 */

static PyObject *getFlagAttr( BPy_Camera *self, void *type )
{
	if (self->camera->flag & (int)type)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}



/*
 * set floating point attributes which require clamping
 */

static int setFlagAttr( BPy_Camera *self, PyObject *value, void *type )
{
	if (PyObject_IsTrue(value))
		self->camera->flag |= (int)type;
	else
		self->camera->flag &= ~(int)type;
	return 0;
}


/*****************************************************************************/
/* Python attributes get/set structure:                                      */
/*****************************************************************************/
static PyGetSetDef BPy_Camera_getseters[] = {
	{"name",
	 (getter)Camera_getName, (setter)Camera_setName,
	 "Camera name",
	 NULL},
	{"lib",
	 (getter)Camera_getLib, (setter)NULL,
	 "Camera libname",
	 NULL},
	{"users",
	 (getter)Camera_getUsers, (setter)NULL,
	 "Number of camera users",
	 NULL},
	{"fakeUser",
	 (getter)Camera_getFakeUser, (setter)Camera_setFakeUser,
	 "Cameras fake user state",
	 NULL},
	{"type",
	 (getter)Camera_getType, (setter)Camera_setType,
	 "camera type \"persp\" or \"ortho\"",
	 NULL},
	{"mode",
	 (getter)Camera_getMode, (setter)Camera_setMode,
	 "Cameras mode",
	 NULL},
	{"ipo",
	 (getter)Camera_getIpo, (setter)Camera_setIpo,
	 "Cameras ipo",
	 NULL},
	 
	/* float settings */
	{"lens",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "lens angle for perspective cameras",
	 (void *)EXPP_CAM_ATTR_LENS},
	{"scale",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "scale for ortho cameras",
	 (void *)EXPP_CAM_ATTR_SCALE},
	{"clipStart",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "the cameras clip start",
	 (void *)EXPP_CAM_ATTR_CLIPSTART},
	{"clipEnd",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "the cameras clip end",
	 (void *)EXPP_CAM_ATTR_CLIPEND},
	{"dofDist",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "cameras dof distance",
	 (void *)EXPP_CAM_ATTR_DOFDIST},
	{"drawSize",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "the cameras display size",
	 (void *)EXPP_CAM_ATTR_DRAWSIZE},
	{"alpha",
	 (getter)getFloatAttr, (setter)setFloatAttrClamp,
	 "passepart alpha value for display",
	 (void *)EXPP_CAM_ATTR_ALPHA},
	 
	/* flags - use flags as defined in DNA_camera_types.h */
	{"drawLimits",
	 (getter)getFlagAttr, (setter)setFlagAttr,
	 "toggle the draw limits display flag",
	 (void *)CAM_SHOWLIMITS},
	{"drawMist",
	 (getter)getFlagAttr, (setter)setFlagAttr,
	 "toggle the draw mist display flag",
	 (void *)CAM_SHOWMIST},
	{"drawName",
	 (getter)getFlagAttr, (setter)setFlagAttr,
	 "toggle the draw name display flag",
	 (void *)CAM_SHOWNAME},
	{"drawTileSafe",
	 (getter)getFlagAttr, (setter)setFlagAttr,
	 "toggle the tile safe display flag",
	 (void *)CAM_SHOWTITLESAFE},
	{"drawPassepartout",
	 (getter)getFlagAttr, (setter)setFlagAttr,
	 "toggle the passPartOut display flag",
	 (void *)CAM_SHOWPASSEPARTOUT},
	{NULL,NULL,NULL,NULL,NULL}  /* Sentinel */
};


/*****************************************************************************/
/* Python Camera_Type structure definition:                               */
/*****************************************************************************/
PyTypeObject Camera_Type = {
	PyObject_HEAD_INIT( NULL )  /* required py macro */
	0,                          /* ob_size */
	/*  For printing, in format "<module>.<name>" */
	"Blender Camera",           /* char *tp_name; */
	sizeof( BPy_Camera ),       /* int tp_basicsize; */
	0,                          /* tp_itemsize;  For allocation */

	/* Methods to implement standard operations */

	( destructor ) Camera_dealloc,/* destructor tp_dealloc; */
	NULL,                       /* printfunc tp_print; */
	NULL,                       /* getattrfunc tp_getattr; */
	NULL,                       /* setattrfunc tp_setattr; */
	( cmpfunc ) Camera_compare, /* cmpfunc tp_compare; */
	( reprfunc ) Camera_repr,   /* reprfunc tp_repr; */

	/* Method suites for standard classes */

	NULL,                       /* PyNumberMethods *tp_as_number; */
	NULL,                       /* PySequenceMethods *tp_as_sequence; */
	NULL,                       /* PyMappingMethods *tp_as_mapping; */

	/* More standard operations (here for binary compatibility) */

	NULL,                       /* hashfunc tp_hash; */
	NULL,                       /* ternaryfunc tp_call; */
	NULL,                       /* reprfunc tp_str; */
	NULL,                       /* getattrofunc tp_getattro; */
	NULL,                       /* setattrofunc tp_setattro; */

	/* Functions to access object as input/output buffer */
	NULL,                       /* PyBufferProcs *tp_as_buffer; */

  /*** Flags to define presence of optional/expanded features ***/
	Py_TPFLAGS_DEFAULT,         /* long tp_flags; */

	NULL,                       /*  char *tp_doc;  Documentation string */
  /*** Assigned meaning in release 2.0 ***/
	/* call function for all accessible objects */
	NULL,                       /* traverseproc tp_traverse; */

	/* delete references to contained objects */
	NULL,                       /* inquiry tp_clear; */

  /***  Assigned meaning in release 2.1 ***/
  /*** rich comparisons ***/
	NULL,                       /* richcmpfunc tp_richcompare; */

  /***  weak reference enabler ***/
	0,                          /* long tp_weaklistoffset; */

  /*** Added in release 2.2 ***/
	/*   Iterators */
	NULL, /* getiterfunc tp_iter; */
	NULL, /* iternextfunc tp_iternext; */

  /*** Attribute descriptor and subclassing stuff ***/
	BPy_Camera_methods,       /* struct PyMethodDef *tp_methods; */
	NULL,                       /* struct PyMemberDef *tp_members; */
	BPy_Camera_getseters,                       /* struct PyGetSetDef *tp_getset; */
	NULL,                       /* struct _typeobject *tp_base; */
	NULL,                       /* PyObject *tp_dict; */
	NULL,                       /* descrgetfunc tp_descr_get; */
	NULL,                       /* descrsetfunc tp_descr_set; */
	0,                          /* long tp_dictoffset; */
	NULL,                       /* initproc tp_init; */
	NULL,                       /* allocfunc tp_alloc; */
	NULL,                       /* newfunc tp_new; */
	/*  Low-level free-memory routine */
	NULL,                       /* freefunc tp_free;  */
	/* For PyObject_IS_GC */
	NULL,                       /* inquiry tp_is_gc;  */
	NULL,                       /* PyObject *tp_bases; */
	/* method resolution order */
	NULL,                       /* PyObject *tp_mro;  */
	NULL,                       /* PyObject *tp_cache; */
	NULL,                       /* PyObject *tp_subclasses; */
	NULL,                       /* PyObject *tp_weaklist; */
	NULL
};



static int Camera_compare( BPy_Camera * a, BPy_Camera * b )
{
	Camera *pa = a->camera, *pb = b->camera;
	return ( pa == pb ) ? 0 : -1;
}

static PyObject *Camera_repr( BPy_Camera * self )
{
	return PyString_FromFormat( "[Camera \"%s\"]",
				    self->camera->id.name + 2 );
}

/*
 * Camera_insertIpoKey()
 *  inserts Camera IPO key for LENS and CLIPPING
 */

static PyObject *Camera_insertIpoKey( BPy_Camera * self, PyObject * args )
{
	int key = 0;

	if( !PyArg_ParseTuple( args, "i", &( key ) ) )
		return ( EXPP_ReturnPyObjError( PyExc_AttributeError,
										"expected int argument" ) );

	if (key == IPOKEY_LENS){
		insertkey((ID *)self->camera, ID_CA, NULL, NULL, CAM_LENS);     
	}
	else if (key == IPOKEY_CLIPPING){
		insertkey((ID *)self->camera, ID_CA, NULL, NULL, CAM_STA);
		insertkey((ID *)self->camera, ID_CA, NULL, NULL, CAM_END);   
	}

	allspace(REMAKEIPO, 0);
	EXPP_allqueue(REDRAWIPO, 0);
	EXPP_allqueue(REDRAWVIEW3D, 0);
	EXPP_allqueue(REDRAWACTION, 0);
	EXPP_allqueue(REDRAWNLA, 0);

	Py_RETURN_NONE;
}
