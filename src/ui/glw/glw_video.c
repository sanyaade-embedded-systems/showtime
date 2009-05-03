/*
 *  Video output on GL surfaces
 *  Copyright (C) 2007 Andreas Öman
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
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "media.h"
#include "glw_video.h"
//#include "subtitles.h"
#include "video/yadif.h"
#include "video/video_playback.h"
#if ENABLE_DVD
#include "video/video_dvdspu.h"
#endif

static const char *yuv2rbg_code =
#include "cg/yuv2rgb.h"
;

static const char *yuv2rbg_2mix_code =
#include "cg/yuv2rgb_2mix.h"
;

static void gv_purge_queues(video_decoder_t *vd);


/**************************************************************************
 *
 *  GL Video Init
 *
 */


static int
glp_check_error(const char *name)
{
  GLint errpos;
  const GLubyte *errstr;

  glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);

  if(errpos == -1)
    return 0;

  errstr = glGetString(GL_PROGRAM_ERROR_STRING_ARB);

  TRACE(TRACE_ERROR, "OpenGL Video", 
	"%s: error \"%s\" on line %d", name, errstr, errpos);
  return 0;
}

/**
 *
 */
void
glw_video_global_init(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  GLint tu;

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &tu);
  if(tu < 6) {
    TRACE(TRACE_ERROR, "OpenGL", 
	  "Insufficient number of texture image units (%d) "
	  "for GLW video rendering widget. Video output will be corrupted",
	  tu);
  } else {
    TRACE(TRACE_DEBUG, "OpenGL", "%d texture image units available", tu);
  }

  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_code), yuv2rbg_code);

  glp_check_error("yuv2rgb");


  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_2mix_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_2mix_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_2mix_code), yuv2rbg_2mix_code);

  glp_check_error("yuv2rgb_2mix");

  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
}

/**
 *
 */
void
glw_video_global_flush(glw_root_t *gr)
{
  glw_video_t *gv;
  video_decoder_t *vd;

  LIST_FOREACH(gv, &gr->gr_be.gbr_video_decoders, gv_global_link) {
    vd = gv->gv_vd;

    hts_mutex_lock(&vd->vd_queue_mutex);
    gv_purge_queues(vd);
    hts_cond_signal(&vd->vd_avail_queue_cond);
    hts_cond_signal(&vd->vd_bufalloced_queue_cond);
    hts_mutex_unlock(&vd->vd_queue_mutex);
  }
}



/**
 *  Buffer allocator
 */
static void
gv_buffer_allocator(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;
  video_decoder_frame_t *vdf;
  size_t siz;

  hts_mutex_lock(&vd->vd_queue_mutex);
  
  while(vd->vd_active_frames < vd->vd_active_frames_needed) {
    vdf = calloc(1, sizeof(gl_video_frame_t));
    TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_avail_queue_cond);
    vd->vd_active_frames++;
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);

    gvf = (gl_video_frame_t *)vdf;

    if(gvf->gvf_pbo != 0)
      glDeleteBuffersARB(1, &gvf->gvf_pbo);
    glGenBuffersARB(1, &gvf->gvf_pbo);


    /* XXX: Do we really need to delete textures if they are already here ? */
       
    if(gvf->gvf_textures[0] != 0)
      glDeleteTextures(3, gvf->gvf_textures);

    glGenTextures(3, gvf->gvf_textures);

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

    siz = 
      vdf->vdf_width[0] * vdf->vdf_height[0] + 
      vdf->vdf_width[1] * vdf->vdf_height[1] + 
      vdf->vdf_width[2] * vdf->vdf_height[2];

    glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, siz, NULL, GL_STREAM_DRAW_ARB);

    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);

    gvf->gvf_pbo_offset[0] = 0;
    gvf->gvf_pbo_offset[1] = vdf->vdf_width[0] * vdf->vdf_height[0];
    gvf->gvf_pbo_offset[2] = 
      gvf->gvf_pbo_offset[1] + vdf->vdf_width[1] * vdf->vdf_height[1];

    vdf->vdf_data[0] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[0];
    vdf->vdf_data[1] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[1];
    vdf->vdf_data[2] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[2];

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    TAILQ_INSERT_TAIL(&vd->vd_bufalloced_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_bufalloced_queue_cond);
  }

  hts_mutex_unlock(&vd->vd_queue_mutex);

}

/**************************************************************************
 *
 *  Texture loader
 *
 */


static void
gv_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint 
gv_tex_get(glw_video_t *gv, gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


static void
video_frame_upload(glw_video_t *gv, gl_video_frame_t *gvf)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr = NULL;

  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_L));
  gv_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1, 
	       gvf->gvf_vdf.vdf_width[0], gvf->gvf_vdf.vdf_height[0],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[0]);

  /* Cr */

  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_Cr));
  gv_set_tex_meta();

  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_vdf.vdf_width[1], gvf->gvf_vdf.vdf_height[1],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[2]);

  /* Cb */
  
  
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_Cb));

  gv_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_vdf.vdf_width[2], gvf->gvf_vdf.vdf_height[2],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[1]);
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

}




/**************************************************************************
 *
 *  Video widget layout
 *
 */



static void
gv_enqueue_for_decode(video_decoder_t *vd, video_decoder_frame_t *vdf,
		      struct video_decoder_frame_queue *fromqueue)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;

  hts_mutex_lock(&vd->vd_queue_mutex);

  TAILQ_REMOVE(fromqueue, vdf, vdf_link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
  
    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);

    vdf->vdf_data[0] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[0];
    vdf->vdf_data[1] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[1];
    vdf->vdf_data[2] = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[2];

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
  hts_cond_signal(&vd->vd_avail_queue_cond);
  hts_mutex_unlock(&vd->vd_queue_mutex);
}


static void
gv_enqueue_for_display(video_decoder_t *vd, video_decoder_frame_t *vdf,
		       struct video_decoder_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, vdf, vdf_link);
  TAILQ_INSERT_TAIL(&vd->vd_displaying_queue, vdf, vdf_link);
}


static float cmatrix_color[9] = {
  1.1643,  0,        1.5958,
  1.1643, -0.39173, -0.81290,
  1.1643,  2.017,    0
};
#if 0
static float cmatrix_bw[9] = {
  1.1643,  0,        0,
  1.1643,  0,        0,
  1.1643,  0,        0
};
#endif

static void
gv_color_matrix_update(glw_video_t *gv, media_pipe_t *mp)
{
  float *f;
  int i;

  //  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  f = cmatrix_color;

  for(i = 0; i < 9; i++)
    gv->gv_cmatrix[i] = (gv->gv_cmatrix[i] * 15.0 + f[i]) / 16.0;

}





static int
compute_output_duration(video_decoder_t *vd, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(vd->vd_avdiff_x > 0) {
    delta = pow(vd->vd_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(vd->vd_avdiff_x < 0) {
    delta = -pow(-vd->vd_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}

static void
compute_avdiff(video_decoder_t *vd, media_pipe_t *mp, int64_t pts, int epoch)
{
  int64_t rt;
  int64_t aclock;

  if(mp->mp_audio_clock_epoch != epoch) {
    /* Not the same clock epoch, can not sync */
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }

  if(vd->vd_compensate_thres > 0) {
    vd->vd_compensate_thres--;
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }
  

  hts_mutex_lock(&mp->mp_clock_mutex);

  rt = showtime_get_ts();

  aclock = mp->mp_audio_clock + rt - mp->mp_audio_clock_realtime;

  hts_mutex_unlock(&mp->mp_clock_mutex);

  vd->vd_avdiff = aclock - (pts - 16666) - vd->vd_avd_delta;

  if(abs(vd->vd_avdiff) < 10000000) {

    vd->vd_avdiff_x = kalman(&vd->vd_avfilter, (float)vd->vd_avdiff / 1000000);
    if(vd->vd_avdiff_x > 10.0f)
      vd->vd_avdiff_x = 10.0f;
    
    if(vd->vd_avdiff_x < -10.0f)
      vd->vd_avdiff_x = -10.0f;
  }

#if 0
 {
   static int64_t lastpts, lastaclock;
   
  printf("%s: AVDIFF = %10f %10d %15lld %15lld %15lld %15lld %15lld\n", 
	 mp->mp_name, vd->vd_avdiff_x, vd->vd_avdiff,
	 aclock, aclock - lastaclock, pts, pts - lastpts,
	 mp->mp_audio_clock);
  lastpts = pts;
  lastaclock = aclock;
 }
#endif
}


static int64_t
gv_compute_blend(glw_video_t *gv, video_decoder_frame_t *fra,
		 video_decoder_frame_t *frb, int output_duration)
{
  int64_t pts;
  int x;

  if(fra->vdf_duration >= output_duration) {
  
    gv->gv_fra = fra;
    gv->gv_frb = NULL;

    pts = fra->vdf_pts;

    fra->vdf_duration -= output_duration;
    fra->vdf_pts      += output_duration;

  } else if(frb != NULL) {

    gv->gv_fra = fra;
    gv->gv_frb = frb;
    gv->gv_blend = (float) fra->vdf_duration / (float)output_duration;

    if(fra->vdf_duration + 
       frb->vdf_duration < output_duration) {

      fra->vdf_duration = 0;
      pts = frb->vdf_pts;

    } else {
      pts = fra->vdf_pts;
      x = output_duration - fra->vdf_duration;
      frb->vdf_duration -= x;
      frb->vdf_pts      += x;
    }
    fra->vdf_duration = 0;

  } else {
    gv->gv_fra = fra;
    gv->gv_frb = NULL;
    fra->vdf_pts      += output_duration;

    pts = fra->vdf_pts;
  }

  return pts;
}



#if ENABLE_DVD
/**
 *
 */
static void
spu_repaint(glw_video_t *gv, dvdspu_decoder_t *dd, dvdspu_t *d)
{
  int width  = d->d_x2 - d->d_x1;
  int height = d->d_y2 - d->d_y1;
  int outsize = width * height * 4;
  uint32_t *tmp, *t0; 
  int x, y, i;
  uint8_t *buf = d->d_bitmap;
  pci_t *pci = &dd->dd_pci;
  dvdnav_highlight_area_t ha;
  int hi_palette[4];
  int hi_alpha[4];

  if(dd->dd_clut == NULL)
    return;
  
  gv->gv_in_menu = pci->hli.hl_gi.hli_ss;

  if(pci->hli.hl_gi.hli_ss &&
     dvdnav_get_highlight_area(pci, dd->dd_curbut, 0, &ha) 
     == DVDNAV_STATUS_OK) {

    hi_alpha[0] = (ha.palette >>  0) & 0xf;
    hi_alpha[1] = (ha.palette >>  4) & 0xf;
    hi_alpha[2] = (ha.palette >>  8) & 0xf;
    hi_alpha[3] = (ha.palette >> 12) & 0xf;
     
    hi_palette[0] = (ha.palette >> 16) & 0xf;
    hi_palette[1] = (ha.palette >> 20) & 0xf;
    hi_palette[2] = (ha.palette >> 24) & 0xf;
    hi_palette[3] = (ha.palette >> 28) & 0xf;
  }

  t0 = tmp = malloc(outsize);


  ha.sx -= d->d_x1;
  ha.ex -= d->d_x1;

  ha.sy -= d->d_y1;
  ha.ey -= d->d_y1;

  /* XXX: this can be optimized in many ways */

  for(y = 0; y < height; y++) {
    for(x = 0; x < width; x++) {
      i = buf[0];

      if(pci->hli.hl_gi.hli_ss &&
	 x >= ha.sx && y >= ha.sy && x <= ha.ex && y <= ha.ey) {

	if(hi_alpha[i] == 0) {
	  *tmp = 0;
	} else {
	  *tmp = dd->dd_clut[hi_palette[i] & 0xf] | 
	    ((hi_alpha[i] * 0x11) << 24);
	}

      } else {

	if(d->d_alpha[i] == 0) {
	  
	  /* If it's 100% transparent, write RGB as zero too, or weird
	     aliasing effect will occure when GL scales texture */
	  
	  *tmp = 0;
	} else {
	  *tmp = dd->dd_clut[d->d_palette[i] & 0xf] | 
	    ((d->d_alpha[i] * 0x11) << 24);
	}
      }

      buf++;
      tmp++;
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
	       width, height, 0,
	       GL_RGBA, GL_UNSIGNED_BYTE, t0);

  free(t0);
}




static void
spu_layout(glw_video_t *gv, dvdspu_decoder_t *dd, int64_t pts)
{
  dvdspu_t *d;
  int x;

  hts_mutex_lock(&dd->dd_mutex);

 again:
  d = TAILQ_FIRST(&dd->dd_queue);

  if(d == NULL) {
    hts_mutex_unlock(&dd->dd_mutex);
    return;
  }

  if(d->d_destroyme == 1)
    goto destroy;

  x = dvdspu_decode(d, pts);

  switch(x) {
  case -1:
  destroy:
    dvdspu_destroy(dd, d);
    gv->gv_in_menu = 0;

    glDeleteTextures(1, &gv->gv_sputex);
    gv->gv_sputex = 0;
    goto again;

  case 0:
    if(dd->dd_repaint == 0)
      break;

    dd->dd_repaint = 0;
    /* FALLTHRU */

  case 1:
    if(gv->gv_sputex == 0) {
      glGenTextures(1, &gv->gv_sputex);

      glBindTexture(GL_TEXTURE_2D, gv->gv_sputex);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    } else {
      glBindTexture(GL_TEXTURE_2D, gv->gv_sputex);
    }
    spu_repaint(gv, dd, d);
    break;
  }
  hts_mutex_unlock(&dd->dd_mutex);
}
#endif



static void 
gv_new_frame(video_decoder_t *vd, glw_video_t *gv)
{
  video_decoder_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  struct video_decoder_frame_queue *dq;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  gv_color_matrix_update(gv, mp);
  output_duration = compute_output_duration(vd, frame_duration);


  dq = &vd->vd_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&vd->vd_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      gv->gv_fra = fra;
      gv->gv_frb = NULL;
    } else {
      gv->gv_fra = NULL;
      gv->gv_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL)
      gv_enqueue_for_decode(vd, frb, &vd->vd_displaying_queue);
      
    frb = TAILQ_NEXT(fra, vdf_link);

    pts = gv_compute_blend(gv, fra, frb, output_duration);
    epoch = fra->vdf_epoch;

    if(!vd->vd_hold || frb != NULL) {
      if(fra != NULL && fra->vdf_duration == 0)
	gv_enqueue_for_display(vd, fra, dq);
    }
    if(frb != NULL && frb->vdf_duration == 0)
      gv_enqueue_for_display(vd, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    compute_avdiff(vd, mp, pts, epoch);

#if ENABLE_DVD
    if(vd->vd_dvdspu != NULL)
      spu_layout(gv, vd->vd_dvdspu, pts);
#endif

  }

}




/**
 *  Video widget render
 */
static void
render_video_quad(video_decoder_t *vd)
{
  float tzoom = vd->vd_interlaced ? 0.01 : 0.00;
  
  glBegin(GL_QUADS);

  glTexCoord2f(tzoom, tzoom);
  glVertex3f( -1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(1.0 - tzoom, tzoom);
  glVertex3f( 1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(1.0 - tzoom, 1.0 - tzoom);
  glVertex3f( 1.0f, -1.0f, 0.0f);

  glTexCoord2f(tzoom, 1.0 - tzoom); 
  glVertex3f( -1.0f, -1.0f, 0.0f);

  glEnd();
}




static void
render_video_1f(glw_video_t *gv, video_decoder_t *vd,
		video_decoder_frame_t *vdf, float alpha)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;
  int i;
  GLuint tex;

  video_frame_upload(gv, gvf);

  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_prog);


  /* ctrl constants */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha,
			       /* ctrl.y == ishift */
			       (-0.5f * vdf->vdf_debob) / 
			       (float)vdf->vdf_height[0],
			       0.0,
			       0.0);

  /* color matrix */

  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_Cb);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_Cr);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_L);
  glBindTexture(GL_TEXTURE_2D, tex);
  
  render_video_quad(vd);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


static void
gv_blend_frames(glw_video_t *gv, video_decoder_t *vd, 
		video_decoder_frame_t *fra, video_decoder_frame_t *frb,
		float alpha)
{
  float blend = gv->gv_blend;
  gl_video_frame_t *gvf_a = (gl_video_frame_t *)fra;
  gl_video_frame_t *gvf_b = (gl_video_frame_t *)frb;
  int i;
  
  video_frame_upload(gv, gvf_a);
  video_frame_upload(gv, gvf_b);
    
  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_2mix_prog);

  /* ctrl */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha, 
			       /* ctrl.y == blend */
			       blend,
			       /* ctrl.z == image a, y displace */
			       (-0.5f * fra->vdf_debob) / 
			       (float)fra->vdf_height[0],
			       /* ctrl.w == image b, y displace */
			       (-0.5f * frb->vdf_debob) / 
			       (float)frb->vdf_height[0]);
				
  /* color matrix */
  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_b, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_b, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_b, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_a, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_a, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf_a, GVF_TEX_L));

  render_video_quad(vd);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}




static void 
render_video(glw_t *w, video_decoder_t *vd, glw_video_t *gv, glw_rctx_t *rc)
{
  video_decoder_frame_t *fra, *frb;
  int width = 0, height = 0;
#if ENABLE_DVD
  dvdspu_decoder_t *dd;
  dvdspu_t *d;
#endif
  
  /*
   * rescale
   */
 
  glPushMatrix();

#if 0 
  if(gv->gv_zoom != 100)
    glScalef(gv->gv_zoom / 100.0f, gv->gv_zoom / 100.0f, 1.0f);
#endif

  fra = gv->gv_fra;
  frb = gv->gv_frb;

  glw_rescale(rc, vd->vd_aspect);

  if(fra != NULL && glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  

  if(fra != NULL) {

    width = fra->vdf_width[0];
    height = fra->vdf_height[0];

    if(frb != NULL) {
      gv_blend_frames(gv, vd, fra, frb, rc->rc_alpha);
    } else {
      render_video_1f(gv, vd, fra, rc->rc_alpha);
    }
  }

  gv->gv_width  = width;
  gv->gv_height = height;

  glEnable(GL_BLEND); 

#if ENABLE_DVD
  dd = vd->vd_dvdspu;
  if(gv->gv_sputex != 0 && dd != NULL && width > 0 &&
     (glw_is_focused(w) || !dd->dd_pci.hli.hl_gi.hli_ss)) {
    d = TAILQ_FIRST(&dd->dd_queue);

    if(d != NULL) {
      glBindTexture(GL_TEXTURE_2D, gv->gv_sputex);

      glPushMatrix();

      glScalef(2.0f / width, -2.0f / height, 0.0f);
      glTranslatef(-width / 2, -height / 2, 0.0f);
      
      glColor4f(1.0, 1.0, 1.0, rc->rc_alpha);
      
      glBegin(GL_QUADS);
      
      glTexCoord2f(0, 0.0);
      glVertex3f(d->d_x1, d->d_y1, 0.0f);
    
      glTexCoord2f(1.0, 0.0);
      glVertex3f(d->d_x2, d->d_y1, 0.0f);
    
      glTexCoord2f(1.0, 1.0);
      glVertex3f(d->d_x2, d->d_y2, 0.0f);
    
      glTexCoord2f(0, 1.0);
      glVertex3f(d->d_x1, d->d_y2, 0.0f);

      glEnd();

      glPopMatrix();
    }
  }
#endif

  glPopMatrix();
}

/**
 * 
 */
static void
vdf_purge(video_decoder_t *vd, video_decoder_frame_t *vdf)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;
  
  if(gvf->gvf_pbo != 0) {
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
    glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
    gvf->gvf_pbo_ptr = NULL;
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    glDeleteBuffersARB(1, &gvf->gvf_pbo);
  }

  gvf->gvf_pbo = 0;

  if(gvf->gvf_textures[0] != 0)
    glDeleteTextures(3, gvf->gvf_textures);

  gvf->gvf_textures[0] = 0;

  free(vdf);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
}



static void
gv_purge_queues(video_decoder_t *vd)
{
  video_decoder_frame_t *vdf;

  while((vdf = TAILQ_FIRST(&vd->vd_avail_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_avail_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloced_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_displaying_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_display_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_display_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }
}


/**
 *
 */
static int
gl_video_widget_event(glw_video_t *gv, event_t *e)
{
  switch(e->e_type) {
  case EVENT_PLAYPAUSE:
  case EVENT_PLAY:
  case EVENT_PAUSE:
  case EVENT_ENTER:
    mp_enqueue_event(gv->gv_mp, e);
    return 1;

  case EVENT_UP:
  case EVENT_DOWN:
  case EVENT_LEFT:
  case EVENT_RIGHT:
    if(!gv->gv_in_menu)
      return 0;
    mp_enqueue_event(gv->gv_mp, e);
    return 1;

  default:
    return 0;
  }
}



#if ENABLE_DVD
/**
 *
 */
static int
pointer_event(glw_video_t *gv, glw_pointer_event_t *gpe)
{
  dvdspu_decoder_t *dd = gv->gv_vd->vd_dvdspu;
  pci_t *pci;
  int x, y;
  int32_t button, best, dist, d, mx, my, dx, dy;
  event_t *e;

  if(dd == NULL)
    return 0;

  pci = &dd->dd_pci;

  if(!pci->hli.hl_gi.hli_ss)
    return 1;
  
  x = (0.5 +  0.5 * gpe->x) * (float)gv->gv_width;
  y = (0.5 + -0.5 * gpe->y) * (float)gv->gv_height;

  best = 0;
  dist = 0x08000000; /* >> than  (720*720)+(567*567); */
  
  /* Loop through all buttons */
  for(button = 1; button <= pci->hli.hl_gi.btn_ns; button++) {
    btni_t *button_ptr = &(pci->hli.btnit[button-1]);

    if((x >= button_ptr->x_start) && (x <= button_ptr->x_end) &&
       (y >= button_ptr->y_start) && (y <= button_ptr->y_end)) {
      mx = (button_ptr->x_start + button_ptr->x_end)/2;
      my = (button_ptr->y_start + button_ptr->y_end)/2;
      dx = mx - x;
      dy = my - y;
      d = (dx*dx) + (dy*dy);
      /* If the mouse is within the button and the mouse is closer
       * to the center of this button then it is the best choice. */
      if(d < dist) {
        dist = d;
        best = button;
      }
    }
  }

  if(best == 0)
    return 1;

  switch(gpe->type) {
  case GLW_POINTER_CLICK:
    e = event_create(EVENT_DVD_ACTIVATE_BUTTON, sizeof(event_t) + 1);
    break;

  case GLW_POINTER_MOTION:
    if(dd->dd_curbut == best)
      return 1;

    e = event_create(EVENT_DVD_SELECT_BUTTON, sizeof(event_t) + 1);
    break;

  default:
    return 1;
  }

  e->e_payload[0] = best;
  mp_enqueue_event(gv->gv_mp, e);
  event_unref(e);
  return 1;
}
#endif

/**
 *
 */
static void
gv_update_focusable(video_decoder_t *vd, glw_video_t *gv)
{
  int want_focus = 0;
#if ENABLE_DVD
  dvdspu_decoder_t *dd = gv->gv_vd->vd_dvdspu;

  if(dd != NULL) {
    pci_t *pci = &dd->dd_pci;

    if(pci->hli.hl_gi.hli_ss)
      want_focus = 1;
  }
#endif
  
  glw_set_i(&gv->w, 
	    GLW_ATTRIB_FOCUS_WEIGHT, want_focus ? 1.0 : 0.0, 
	    NULL);
}


/**
 *
 */
static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
			 void *extra)
{
  glw_root_t *gr = w->glw_root;
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    /* We are going away, flush out all frames (PBOs and textures)
       and destroy zombie video decoder */

    if(gv->gv_sputex)
      glDeleteTextures(1, &gv->gv_sputex);

    LIST_REMOVE(gv, gv_global_link);
    gv_purge_queues(vd);
    video_decoder_destroy(vd);
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video(w, vd, gv, extra);
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    gv_buffer_allocator(vd);
    gv_new_frame(vd, gv);
    gv_update_focusable(vd, gv);
    return 0;

  case GLW_SIGNAL_EVENT:
    return gl_video_widget_event(gv, extra);

  case GLW_SIGNAL_DESTROY:
    prop_add_int(gr->gr_fullscreen_req, -1);
    video_playback_destroy(gv->gv_vp);
    video_decoder_stop(vd);
    mp_ref_dec(gv->gv_mp);
    gv->gv_mp = NULL;
    return 0;

#if ENABLE_DVD
  case GLW_SIGNAL_POINTER_EVENT:
    return pointer_event(gv, extra);
#endif

  default:
    return 0;
  }
}


/**
 *
 */
static void
glw_video_init(glw_video_t *gv, glw_root_t *gr)
{
  //  gv->gv_dvdspu = gl_dvdspu_init();
 
  LIST_INSERT_HEAD(&gr->gr_be.gbr_video_decoders, gv, gv_global_link);

  gv->gv_zoom = 100;
}



/**
 *
 */
void
glw_video_ctor(glw_t *w, int init, va_list ap)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  const char *filename = NULL;
  prop_t *p, *p2, *pm;
  event_t *e;

  if(init) {

    gv->gv_mp = mp_create("Video decoder", "video");

    glw_signal_handler_int(w, gl_video_widget_callback);
    glw_video_init(gv, gr);

    glw_set_i(w, 
	      GLW_ATTRIB_SET_FLAGS, GLW_EVERY_FRAME, 
	      NULL);

    gv->gv_vd = video_decoder_create(gv->gv_mp);
    gv->gv_vp = video_playback_create(gv->gv_mp);

    prop_add_int(gr->gr_fullscreen_req, 1);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_PROPROOT:
      p = va_arg(ap, void *);

      p2 = prop_create(p, "video");
      pm = prop_create(p2, "metadata");
      
      prop_link(gv->gv_mp->mp_prop_root, pm);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL && filename[0] != 0) {
    e = event_create_url(EVENT_PLAY_URL, filename);
    mp_enqueue_event(gv->gv_mp, e);
    event_unref(e);
  }
}
