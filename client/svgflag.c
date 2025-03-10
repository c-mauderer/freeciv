/***********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "mem.h"
#include "support.h"

/* client */
#include "canvas_g.h"
#include "sprite_g.h"

#include "svgflag.h"


bool _prefer_svg = FALSE;

static const char **ordered_extensions = NULL;

/**********************************************************************//**
  Enable svg flag features.
**************************************************************************/
bool svg_flag_enable(void)
{
  _prefer_svg = TRUE;

  return TRUE;
}

/**********************************************************************//**
  Return gfx file extensions, in adjusted order.
**************************************************************************/
const char **ordered_gfx_fextensions(void)
{
  const char **incoming;

  if (ordered_extensions != NULL) {
    return ordered_extensions;
  }

  incoming = gfx_fileextensions();

  if (is_svg_flag_enabled()) {
    int count;
    int svg = -1;
    int i;

    for (count = 0; incoming[count] != NULL; count++) {
      if (!fc_strcasecmp("svg", incoming[count])) {
        svg = count;
      }
    }

    if (svg < 0) {
      /* No svg, disable svg flag features */
      _prefer_svg = FALSE;

      ordered_extensions = incoming;
    }

    ordered_extensions = fc_malloc((count + 1) * sizeof(char *));

    /* Put svg first */
    ordered_extensions[0] = incoming[svg];

    for (i = 0; i < svg; i++) {
      ordered_extensions[i + 1] = incoming[i];
    }
    for (; i < count; i++) {
      ordered_extensions[i] = incoming[i];
    }

  } else {
    ordered_extensions = incoming;
  }

  return ordered_extensions;
}

/**********************************************************************//**
  Return gfx file extensions, in adjusted order.
**************************************************************************/
void free_svg_flag_API(void)
{
  if (ordered_extensions != NULL) {
    if (is_svg_flag_enabled()) {
      free(ordered_extensions);
    }

    ordered_extensions = NULL;
  }
}

/**********************************************************************//**
  Fill flag dimensions to rect.
**************************************************************************/
void get_flag_dimensions(struct sprite *flag, struct area_rect *rect,
                         int svg_height)
{
  get_sprite_dimensions(flag, &(rect->w), &(rect->h));

  if (is_svg_flag_enabled()) {
    rect->h = rect->h * svg_height / rect->w;
    rect->w = svg_height;
  }
}

/**********************************************************************//**
  Put flag sprite to canvas.
**************************************************************************/
void canvas_put_flag_sprite(struct canvas *pcanvas,
                            int canvas_x, int canvas_y, int canvas_w, int canvas_h,
                            struct sprite *flag)
{
  if (is_svg_flag_enabled()) {
    canvas_put_sprite_full_scaled(pcanvas, canvas_x, canvas_y, canvas_w, canvas_h,
                                  flag);
  } else {
    canvas_put_sprite_full(pcanvas, canvas_x, canvas_y, flag);
  }
}
