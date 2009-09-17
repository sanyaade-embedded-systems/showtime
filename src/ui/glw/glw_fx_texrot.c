/*
 *  GL Widgets
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include "glw.h"
#include "glw_fx_texrot.h"

/**
 *
 */
static void
glw_fx_texrot_dtor(glw_t *w)
{
  glw_fx_texrot_t *fx = (void *)w;

  glw_gf_unregister(&fx->fx_flushctrl);

  if(fx->fx_tex != NULL)
    glw_tex_deref(w->glw_root, fx->fx_tex);

  if(fx->fx_rtt_initialized)
    glw_rtt_destroy(w->glw_root, &fx->fx_rtt);
}


/**
 *
 */
static void 
glw_fx_texrot_render(glw_t *w, glw_rctx_t *rc)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_loadable_texture_t *glt = fx->fx_tex;
  float a = rc->rc_alpha * w->glw_alpha;

  if(glt != NULL && glt->glt_state == GLT_STATE_VALID && a > 0.01) {
    glw_render(&fx->fx_render, w->glw_root, rc, 
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
	       &glw_rtt_texture(&fx->fx_rtt), 1, 1, 1, a);
  }
}


/**
 *
 */
static void
glw_fx_texrot_init(glw_fx_texrot_t *fx)
{
  int i;

  for(i = 0; i < FX_NPLATES; i++) {
    fx->fx_plates[i].angle = drand48() * 360;
    fx->fx_plates[i].inc = drand48() * 0.1;

    fx->fx_plates[i].x = (drand48() - 0.5) * 0.5;
    fx->fx_plates[i].y = (drand48() - 0.5) * 0.5;
  }
}


/**
 *
 */
static void
glw_fx_texrot_render_internal(glw_root_t *gr, glw_rctx_t *rc,
			      glw_fx_texrot_t *fx, glw_loadable_texture_t *glt)
{
  int i;
  glw_rctx_t rc0 = *rc;

  glw_blendmode(GLW_BLEND_ADDITIVE);

  glw_Scalef(rc, 2.0, 2.0, 1.0);

  for(i = 0; i < FX_NPLATES; i++) {

    glw_PushMatrix(&rc0, rc);

    fx->fx_plates[i].angle += fx->fx_plates[i].inc;

    glw_Translatef(&rc0, fx->fx_plates[i].x, fx->fx_plates[i].y, 0.0);
    glw_Rotatef(&rc0, fx->fx_plates[i].angle, 0.0, 0.0, 1.0);

    glw_render(&fx->fx_source_render, gr, &rc0,
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
	       &glt->glt_texture, 1, 1, 1, 0.15);
    glw_PopMatrix();
  }
  glw_blendmode(GLW_BLEND_NORMAL);
}


/**
 *
 */
static void 
glw_fx_texrot_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_loadable_texture_t *glt = fx->fx_tex;
  glw_root_t *gr = w->glw_root;
  glw_rctx_t rc0;

  int width  = 512;
  int height = 512;

  width  = 256;
  height = 256;

  if(glt == NULL)
    return;

  // Layout the source texture
  glw_tex_layout(w->glw_root, glt);


  if(!fx->fx_source_render_initialized) {
    float xs = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_xs;
    float ys = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_ys;

    glw_render_init(&fx->fx_source_render, 4, GLW_RENDER_ATTRIBS_TEX);
    fx->fx_source_render_initialized = 1;
    
    glw_render_vtx_pos(&fx->fx_source_render, 0, -1.0, -1.0, 0.0);
    glw_render_vtx_st (&fx->fx_source_render, 0,  0.0,  ys);

    glw_render_vtx_pos(&fx->fx_source_render, 1,  1.0, -1.0, 0.0);
    glw_render_vtx_st (&fx->fx_source_render, 1,  xs,   ys);

    glw_render_vtx_pos(&fx->fx_source_render, 2,  1.0,  1.0, 0.0);
    glw_render_vtx_st (&fx->fx_source_render, 2,  xs,   0.0);

    glw_render_vtx_pos(&fx->fx_source_render, 3, -1.0,  1.0, 0.0);
    glw_render_vtx_st (&fx->fx_source_render, 3,  0.0,  0.0);
  }

  // Init render to texture object
  if(!fx->fx_rtt_initialized) {
    fx->fx_rtt_initialized = 1;
    glw_rtt_init(gr, &fx->fx_rtt, width, height);
  }

  // Initialize output texture
  if(!fx->fx_render_initialized) {
    float xs = gr->gr_normalized_texture_coords ? 1.0 : width;
    float ys = gr->gr_normalized_texture_coords ? 1.0 : height;

    glw_render_init(&fx->fx_render, 4, GLW_RENDER_ATTRIBS_TEX);
    fx->fx_render_initialized = 1;

    glw_render_vtx_pos(&fx->fx_render, 0, -1.0, -1.0, 0.0);
    glw_render_vtx_st (&fx->fx_render, 0,  0.0,  ys);

    glw_render_vtx_pos(&fx->fx_render, 1,  1.0, -1.0, 0.0);
    glw_render_vtx_st (&fx->fx_render, 1,  xs,   ys);

    glw_render_vtx_pos(&fx->fx_render, 2,  1.0,  1.0, 0.0);
    glw_render_vtx_st (&fx->fx_render, 2,  xs,   0.0);

    glw_render_vtx_pos(&fx->fx_render, 3, -1.0,  1.0, 0.0);
    glw_render_vtx_st (&fx->fx_render, 3,  0.0,  0.0);
  }

  // Enter render-to-texture mode
  glw_rtt_enter(gr, &fx->fx_rtt, &rc0);

  // Render the gradients
  glw_fx_texrot_render_internal(gr, &rc0, fx, glt);

  // Leave render-to-texture mode
  glw_rtt_restore(gr, &fx->fx_rtt);
}

/**
 *
 */
static int
glw_fx_texrot_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_fx_texrot_layout(w, extra);
    break;
  case GLW_SIGNAL_RENDER:
    glw_fx_texrot_render(w, extra);
    break;
  case GLW_SIGNAL_DTOR:
    glw_fx_texrot_dtor(w);
    break;
  }
  return 0;
}


/**
 *
 */
static void
fxflush(void *aux)
{
  glw_fx_texrot_t *fx = aux;

  if(fx->fx_rtt_initialized) {
    glw_rtt_destroy(fx->w.glw_root, &fx->fx_rtt);
    fx->fx_rtt_initialized = 0;
  }
}


/*
 *
 */
void 
glw_fx_texrot_ctor(glw_t *w, int init, va_list ap)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_attribute_t attrib;
  const char *filename = NULL;

  if(init) {
    glw_signal_handler_int(w, glw_fx_texrot_callback);
    glw_fx_texrot_init(fx);

    /* Flush due to opengl shutdown */
    fx->fx_flushctrl.opaque = fx;
    fx->fx_flushctrl.flush = fxflush;
    glw_gf_register(&fx->fx_flushctrl);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;
    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(filename == NULL)
    return;

  if(fx->fx_tex != NULL)
    glw_tex_deref(w->glw_root, fx->fx_tex);

  fx->fx_tex = glw_tex_create(w->glw_root, filename);
}
