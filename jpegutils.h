/*
 *  jpegutils.h: Some Utility programs for dealing with
 *               JPEG encoded images
 *
 *  Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *  Copyright (C) 2001 pHilipp Zabel  <pzabel@gmx.de>
 *
 *  based on jdatasrc.c and jdatadst.c from the Independent
 *  JPEG Group's software by Thomas G. Lane
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef __JPEGUTILS_H__
#define __JPEGUTILS_H__

#define 	Y4M_ILACE_NONE   0 /** non-interlaced, progressive frame */
#define 	Y4M_ILACE_TOP_FIRST   1 /** interlaced, top-field first */
#define 	Y4M_ILACE_BOTTOM_FIRST   2 /** interlaced, bottom-field first */
#define 	Y4M_ILACE_MIXED   3 /** mixed, "refer to frame header" */
#define 	Y4M_CHROMA_420JPEG   0 /** 4:2:0, H/V centered, for JPEG/MPEG-1 */
#define 	Y4M_CHROMA_420MPEG2   1 /** 4:2:0, H cosited, for MPEG-2 */
#define 	Y4M_CHROMA_420PALDV   2 /** 4:2:0, alternating Cb/Cr, for PAL-DV */
#define 	Y4M_CHROMA_444   3 /** 4:4:4, no subsampling, phew. */
#define 	Y4M_CHROMA_422   4 /** 4:2:2, H cosited */
#define 	Y4M_CHROMA_411   5 /** 4:1:1, H cosited */
#define 	Y4M_CHROMA_MONO   6 /** luma plane only */
#define 	Y4M_CHROMA_444ALPHA   7 /** 4:4:4 with an alpha channel */

 /*
 * jpeg_data:       buffer with input / output jpeg
 * len:             Length of jpeg buffer
 * itype:           Y4M_ILACE_NONE: Not interlaced
 *                  Y4M_ILACE_TOP_FIRST: Interlaced, top-field-first
 *                  Y4M_ILACE_BOTTOM_FIRST: Interlaced, bottom-field-first
 * ctype            Chroma format for decompression.
 *                  Currently always 420 and hence ignored.
 * raw0             buffer with input / output raw Y channel
 * raw1             buffer with input / output raw U/Cb channel
 * raw2             buffer with input / output raw V/Cr channel
 * width            width of Y channel (width of U/V is width/2)
 * height           height of Y channel (height of U/V is height/2)
 */

int decode_jpeg_raw (unsigned char *jpeg_data, int len,
                     int itype, int ctype, int width, int height,
                     unsigned char *raw0, unsigned char *raw1,
                     unsigned char *raw2);
#endif
