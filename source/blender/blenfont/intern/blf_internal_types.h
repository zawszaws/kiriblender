/**
 * $Id: blf_internal_types.h 21247 2009-06-29 21:50:53Z jaguarandi $
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
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 * 
 * Contributor(s): Blender Foundation.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef BLF_INTERNAL_TYPES_H
#define BLF_INTERNAL_TYPES_H

typedef struct GlyphCacheBLF {
	struct GlyphCacheBLF *next;
	struct GlyphCacheBLF *prev;

	/* font size. */
	int size;

	/* and dpi. */
	int dpi;

	/* and the glyphs. */
	ListBase bucket[257];

	/* texture array, to draw the glyphs. */
	GLuint *textures;

	/* size of the array. */
	int ntex;

	/* and the last texture, aka. the current texture. */
	int cur_tex;

	/* like bftgl, we draw every glyph in a big texture, so this is the
	 * current position inside the texture.
	 */
	int x_offs;
	int y_offs;

	/* and the space from one to other. */
	unsigned int pad;

	/* and the bigger glyph in the font. */
	int max_glyph_width;
	int max_glyph_height;

	/* next two integer power of two, to build the texture. */
	int p2_width;
	int p2_height;

	/* number of glyphs in the font. */
	int num_glyphs;

	/* number of glyphs that we load here. */
	int rem_glyphs;

	/* ascender and descender value. */
	float ascender;
	float descender;
} GlyphCacheBLF;

typedef struct GlyphTextureBLF {
	/* texture id where this glyph is store. */
	GLuint tex;

	/* position inside the texture where this glyph is store. */
	int xoff;
	int yoff;

	/* glyph width and height. */
	int width;
	int height;

	/* uv coords. */
	float uv[2][2];

	/* X and Y bearing of the glyph.
	 * The X bearing is from the origin to the glyph left bbox edge.
	 * The Y bearing is from the baseline to the top of the glyph edge.
	 */
	float pos_x;
	float pos_y;
} GlyphTextureBLF;

typedef struct GlyphBitmapBLF {
	/* image data. */
	unsigned char *image;

	int width;
	int height;
	int pitch;

	float pos_x;
	float pos_y;
} GlyphBitmapBLF;

typedef struct GlyphBLF {
	struct GlyphBLF *next;
	struct GlyphBLF *prev;

	/* and the character, as UTF8 */
	unsigned int c;

	/* glyph box. */
	rctf box;

	/* advance size. */
	float advance;

	/* texture information. */
	GlyphTextureBLF *tex_data;

	/* bitmap information. */
	GlyphBitmapBLF *bitmap_data;
} GlyphBLF;

typedef struct FontBLF {
	/* font name. */
	char *name;

	/* filename or NULL. */
	char *filename;

	/* draw mode, texture or bitmap. */
	int mode;

	/* aspect ratio or scale. */
	float aspect;

	/* initial position for draw the text. */
	float pos[3];

	/* angle in degrees. */
	float angle;
	
	/* blur: 3 or 5 large kernel */
	int blur;
	
	/* this is the matrix that we load before rotate/scale/translate. */
	float mat[4][4];

	/* clipping rectangle. */
	rctf clip_rec;

	/* font dpi (default 72). */
	int dpi;

	/* font size. */
	int size;

	/* kerning space, user setting. */
	float kerning;

	/* max texture size. */
	int max_tex_size;

	/* font options. */
	int flags;

	/* list of glyph cache for this font. */
	ListBase cache;

	/* current glyph cache, size and dpi. */
	GlyphCacheBLF *glyph_cache;

	/* freetype2 face. */
	FT_Face face;
} FontBLF;

typedef struct DirBLF {
	struct DirBLF *next;
	struct DirBLF *prev;

	/* full path where search fonts. */
	char *path;
} DirBLF;

#endif /* BLF_INTERNAL_TYPES_H */
