/* Copyright (C) <2010> Douglas Bagnall <douglas@halo.gen.nz>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __SPARROW_SPARROW_H__
#define __SPARROW_SPARROW_H__

#include "gstsparrow.h"

void sparrow_pre_init(GstSparrow *sparrow);
void sparrow_init(GstSparrow *sparrow);

void sparrow_transform(GstSparrow *sparrow, guint8 *bytes);
void sparrow_finalise(GstSparrow *sparrow);


#endif /* __SPARROW_SPARROW_H__ */
