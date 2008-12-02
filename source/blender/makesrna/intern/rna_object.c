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
 * Contributor(s): Blender Foundation (2008).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <stdlib.h>

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_types.h"

#include "rna_internal.h"

#include "DNA_object_types.h"
#include "DNA_property_types.h"

#ifdef RNA_RUNTIME
static struct StructRNA* rna_Object_gameproperties_type(struct CollectionPropertyIterator *iter)
{
	bProperty *property= (bProperty*)iter->ptr.data;
	switch(property->type){
		case PROP_BOOL:
			return &RNA_GameBooleanProperty;
		case PROP_INT:
			return &RNA_GameIntProperty;
		case PROP_FLOAT:
			return &RNA_GameFloatProperty;
		case PROP_STRING:
			return &RNA_GameStringProperty;
		case PROP_TIME:
			return &RNA_GameTimeProperty;
	}
	return &RNA_UnknownType;
}
#else

void RNA_def_object(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna= RNA_def_struct(brna, "Object", "ID", "Object");

	prop= RNA_def_property(srna, "data", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "ID");
	RNA_def_property_ui_text(prop, "Data", "Object data.");

	prop= RNA_def_property(srna, "parent", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_ui_text(prop, "Parent", "Parent Object");

	prop= RNA_def_property(srna, "track", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_ui_text(prop, "Track", "Object being tracked to define the rotation (Old Track).");

	prop= RNA_def_property(srna, "loc", PROP_FLOAT, PROP_VECTOR);
	RNA_def_property_ui_text(prop, "Location", "");
	
	//prop= RNA_def_property(srna, "rot", PROP_FLOAT, PROP_ROTATION);
	//RNA_def_property_ui_text(prop, "Rotation", "");
	prop= RNA_def_property(srna, "rot", PROP_FLOAT, PROP_VECTOR);
	RNA_def_property_ui_text(prop, "Rotation", "");
	
	prop= RNA_def_property(srna, "size", PROP_FLOAT, PROP_VECTOR);
	RNA_def_property_ui_text(prop, "Scale", "");

	prop= RNA_def_property(srna, "modifiers", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "ModifierData");
	RNA_def_property_ui_text(prop, "Modifiers", "Modifiers of this object.");

	prop= RNA_def_property(srna, "sensors", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "Sensor");
	RNA_def_property_ui_text(prop, "Sensors", "Sensors of this object.");

	prop= RNA_def_property(srna, "controllers", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "Controller");
	RNA_def_property_ui_text(prop, "Controller", "Controllers of this object.");

	prop= RNA_def_property(srna, "actuators", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "Actuator");
	RNA_def_property_ui_text(prop, "Actuators", "Actuators of this object.");

	prop= RNA_def_property(srna, "properties", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "prop", NULL);
	RNA_def_property_collection_funcs(prop, 0, 0, 0, 0, "rna_Object_gameproperties_type", 0, 0, 0);
	RNA_def_property_ui_text(prop, "Property", "Properties of this object.");
}

#endif

