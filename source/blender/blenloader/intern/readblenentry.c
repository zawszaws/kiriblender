/**
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
 * .blend file reading entry point
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_linklist.h"

#include "DNA_sdna_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_ID.h"

#include "BKE_utildefines.h" // for ENDB

#include "BKE_main.h"
#include "BKE_library.h" // for free_main

#include "BLO_readfile.h"
#include "BLO_undofile.h"

#include "readfile.h"

#include "BLO_readblenfile.h"

	/**
	 * IDType stuff, I plan to move this
	 * out into its own file + prefix, and
	 * make sure all IDType handling goes through
	 * these routines.
	 */

typedef struct {
	unsigned short code;
	char *name;
	
	int flags;
#define IDTYPE_FLAGS_ISLINKABLE	(1<<0)
} IDType;

static IDType idtypes[]= {
	{ ID_AC,		"Action",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_AR,		"Armature", IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_CA,		"Camera",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_CU,		"Curve",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_GR,		"Group",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_ID,		"ID",		0}, 
	{ ID_IM,		"Image",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_IP,		"Ipo",		IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_KE,		"Key",		0}, 
	{ ID_LA,		"Lamp",		IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_LF,		"Life",		0}, 
	{ ID_LI,		"Library",	0}, 
	{ ID_LT,		"Lattice",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_MA,		"Material", IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_MB,		"Metaball", IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_ME,		"Mesh",		IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_OB,		"Object",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_SAMPLE,	"Sample",	0}, 
	{ ID_SCE,		"Scene",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_SCR,		"Screen",	0}, 
	{ ID_SEQ,		"Sequence",	0}, 
	{ ID_SE,		"Sector",	0}, 
	{ ID_SO,		"Sound",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_TE,		"Texture",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_TXT,		"Text",		IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_VF,		"VFont",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_WO,		"World",	IDTYPE_FLAGS_ISLINKABLE}, 
	{ ID_WV,		"Wave",		0}, 
};
static int nidtypes= sizeof(idtypes)/sizeof(idtypes[0]);

/* local prototypes --------------------- */
void BLO_blendhandle_print_sizes(BlendHandle *, void *); 


static IDType *idtype_from_name(char *str) 
{
	int i= nidtypes;
	
	while (i--)
		if (BLI_streq(str, idtypes[i].name))
			return &idtypes[i];
	
	return NULL;
}
static IDType *idtype_from_code(int code) 
{
	int i= nidtypes;
	
	while (i--)
		if (code==idtypes[i].code)
			return &idtypes[i];
	
	return NULL;
}

static int bheadcode_is_idcode(int code) 
{
	return idtype_from_code(code)?1:0;
}

static int idcode_is_linkable(int code) {
	IDType *idt= idtype_from_code(code);
	return idt?(idt->flags&IDTYPE_FLAGS_ISLINKABLE):0;
}

char *BLO_idcode_to_name(int code) 
{
	IDType *idt= idtype_from_code(code);
	
	return idt?idt->name:NULL;
}

int BLO_idcode_from_name(char *name) 
{
	IDType *idt= idtype_from_name(name);
	
	return idt?idt->code:0;
}
	
	/* Access routines used by filesel. */
	 
BlendHandle *BLO_blendhandle_from_file(char *file) 
{
	BlendReadError err;

	return (BlendHandle*) blo_openblenderfile(file, &err);
}

void BLO_blendhandle_print_sizes(BlendHandle *bh, void *fp) 
{
	FileData *fd= (FileData*) bh;
	BHead *bhead;

	fprintf(fp, "[\n");
	for (bhead= blo_firstbhead(fd); bhead; bhead= blo_nextbhead(fd, bhead)) {
		if (bhead->code==ENDB)
			break;
		else {
			short *sp= fd->filesdna->structs[bhead->SDNAnr];
			char *name= fd->filesdna->types[ sp[0] ];
			char buf[4];
			
			buf[0]= (bhead->code>>24)&0xFF;
			buf[1]= (bhead->code>>16)&0xFF;
			buf[2]= (bhead->code>>8)&0xFF;
			buf[3]= (bhead->code>>0)&0xFF;
			
			buf[0]= buf[0]?buf[0]:' ';
			buf[1]= buf[1]?buf[1]:' ';
			buf[2]= buf[2]?buf[2]:' ';
			buf[3]= buf[3]?buf[3]:' ';
			
			fprintf(fp, "['%.4s', '%s', %d, %ld ], \n", buf, name, bhead->nr, bhead->len+sizeof(BHead));
		}
	}
	fprintf(fp, "]\n");
}

LinkNode *BLO_blendhandle_get_datablock_names(BlendHandle *bh, int ofblocktype) 
{
	FileData *fd= (FileData*) bh;
	LinkNode *names= NULL;
	BHead *bhead;

	for (bhead= blo_firstbhead(fd); bhead; bhead= blo_nextbhead(fd, bhead)) {
		if (bhead->code==ofblocktype) {
			ID *id= (ID*) (bhead+1);
			
			BLI_linklist_prepend(&names, strdup(id->name+2));
		} else if (bhead->code==ENDB)
			break;
	}
	
	return names;
}

LinkNode *BLO_blendhandle_get_linkable_groups(BlendHandle *bh) 
{
	FileData *fd= (FileData*) bh;
	GHash *gathered= BLI_ghash_new(BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp);
	LinkNode *names= NULL;
	BHead *bhead;
	
	for (bhead= blo_firstbhead(fd); bhead; bhead= blo_nextbhead(fd, bhead)) {
		if (bhead->code==ENDB) {
			break;
		} else if (bheadcode_is_idcode(bhead->code)) {
			if (idcode_is_linkable(bhead->code)) {
				char *str= BLO_idcode_to_name(bhead->code);
				
				if (!BLI_ghash_haskey(gathered, str)) {
					BLI_linklist_prepend(&names, strdup(str));
					BLI_ghash_insert(gathered, str, NULL);
				}
			}
		}
	}
	
	BLI_ghash_free(gathered, NULL, NULL);
	
	return names;
}		

void BLO_blendhandle_close(BlendHandle *bh) {
	FileData *fd= (FileData*) bh;
	
	blo_freefiledata(fd);
}

	/**********/

BlendFileData *BLO_read_from_file(char *file, BlendReadError *error_r) 
{
	BlendFileData *bfd = NULL;
	FileData *fd;
		
	fd = blo_openblenderfile(file, error_r);
	if (fd) {
		bfd= blo_read_file_internal(fd, error_r);
		if (bfd) {
			bfd->type= BLENFILETYPE_BLEND;
			strcpy(bfd->main->name, file);
		}
		blo_freefiledata(fd);			
	}

	return bfd;	
}

BlendFileData *BLO_read_from_memory(void *mem, int memsize, BlendReadError *error_r) 
{
	BlendFileData *bfd = NULL;
	FileData *fd;
		
	fd = blo_openblendermemory(mem, memsize,  error_r);
	if (fd) {
		bfd= blo_read_file_internal(fd, error_r);
		if (bfd) {
			bfd->type= BLENFILETYPE_BLEND;
			strcpy(bfd->main->name, "");
		}
		blo_freefiledata(fd);			
	}

	return bfd;	
}

BlendFileData *BLO_read_from_memfile(MemFile *memfile, BlendReadError *error_r) 
{
	BlendFileData *bfd = NULL;
	FileData *fd;
		
	fd = blo_openblendermemfile(memfile, error_r);
	if (fd) {
		bfd= blo_read_file_internal(fd, error_r);
		if (bfd) {
			bfd->type= BLENFILETYPE_BLEND;
			strcpy(bfd->main->name, "");
		}
		blo_freefiledata(fd);			
	}

	return bfd;	
}

void BLO_blendfiledata_free(BlendFileData *bfd)
{
	if (bfd->main) {
		free_main(bfd->main);
	}
	
	if (bfd->user) {
		MEM_freeN(bfd->user);
	}

	MEM_freeN(bfd);
}

char *BLO_bre_as_string(BlendReadError error) 
{
	switch (error) {
	case BRE_NONE:
		return "No error";
	
	case BRE_UNABLE_TO_OPEN:
		return "Unable to open";
	case BRE_UNABLE_TO_READ:
		return "Unable to read";
		
	case BRE_OUT_OF_MEMORY:
		return "Out of memory";
	case BRE_INTERNAL_ERROR:
		return "<internal error>";

	case BRE_NOT_A_BLEND:
		return "File is not a Blender file";
	case BRE_NOT_A_PUBFILE:
		return "File is not a compressed, locked or signed Blender file";
	case BRE_INCOMPLETE:
		return "File incomplete";
	case BRE_CORRUPT:
		return "File corrupt";

	case BRE_TOO_NEW:
		return "File needs newer Blender version, please upgrade";
	case BRE_NOT_ALLOWED:
		return "File is locked";
						
	case BRE_NO_SCREEN:
		return "File has no screen";
	case BRE_NO_SCENE:
		return "File has no scene";
		
	default:
	case BRE_INVALID:
		return "<invalid read error>";
	}
}
