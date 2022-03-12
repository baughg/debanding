/*
 * Copyright (c) 2014 Gary Baugh baughg@tcd.ie
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * video deband filter
 */
#include "libavutil/imgutils.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "vf_deband.h"
#include "vf_pixel_label.c"

typedef struct {
    const AVClass *class;    
    float colour_dist;
    int spatial_dist;
    float dither_strength;
    int kernel_size;
} FlipContext;


static av_cold int init(AVFilterContext *ctx)
{
  FlipContext *s = ctx->priv;
  
  return 0;
}

static int config_input(AVFilterLink *link)
{
    FlipContext *flip = link->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
  static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_NONE
  };
  
  ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
  return 0;
}


static AVFrame *get_video_buffer(AVFilterLink *link, int w, int h)
{
    FlipContext *flip = link->dst->priv;
    AVFrame *frame;
    int i;

    frame = ff_get_video_buffer(link->dst->outputs[0], w, h);
    if (!frame)
        return NULL;
 
    return frame;
}
static inline float rand_float() {
  size_t v = rand()%10000;
  float rfn = (float)v;
  rfn /= 10000;
  
  return rfn;
}

static void deband_frame(uint8_t *dstrow, uint8_t *srcrow, FrameInfo* frame_info,FlipContext *context) {  
  const float spatial_dist_scale = 5.0f;
   
  size_t offset = 0;
  size_t i,j;
  size_t _max_label = 0;
  uint8_t* output_ptr = dstrow;
  const size_t pixel_count = frame_info->height*frame_info->width;
  const size_t src_stride = frame_info->width*3;
  int spatial_distance = context->spatial_dist;
  
  const float colour_distance = context->colour_dist * context->colour_dist;
  const float dither_strength = context->dither_strength;
  int kern_size = context->kernel_size;
  
  if(spatial_distance < 0) {
    if(frame_info->width > 1200) {
      spatial_distance = 13;
    }
    else if(frame_info->width > 720) {
      spatial_distance = 7;
    }
    else {
      spatial_distance = 3;
    }
  }
  
  if(kern_size < 0) {
    if(frame_info->width > 1200) {
      kern_size = 7;
    }
    else if(frame_info->width > 720) {
      kern_size = 5;
    }
    else {
      kern_size = 3;
    }
  }
  
  spatial_distance *= (int)spatial_dist_scale;
    
  pixel_list src_image;
  label_list block_label;
  allocate_pixel(&src_image,pixel_count*3);  
  allocate_label(&block_label,pixel_count);
  
  uint8_t* pel_row = ( uint8_t*)src_image.data_ptr;
  
  for (i = 0; i < frame_info->height; i++) {        
    uint8_t *pel = pel_row;
    uint8_t *src = srcrow;
    
    for (j = 0; j < frame_info->width; j++) {
      offset = 3*j;   
      
      pel[offset + 0] = src[offset + 0];
      pel[offset + 1] = src[offset + 1];
      pel[offset + 2] = src[offset + 2];
      
    }    
    
    srcrow += frame_info->src_stride;
    pel_row += src_stride;
  }
  
  label_list_collection min_field;
  rgb_colour_list block_colour_list;
  
  
  allocate_collection(&min_field,pixel_count); 
  
 
  label(src_image.data_ptr,frame_info->height,frame_info->width,&block_label,&_max_label);
  label_distance(frame_info->height,frame_info->width,&block_label,_max_label,&min_field);
  
  allocate_colour(&block_colour_list,_max_label);
  label_stat(src_image.data_ptr,frame_info->height,frame_info->width,&block_label,_max_label,&block_colour_list);
  
  // interpolation
  label_list hist_label_a; //(max_label+1);
  label_list hist_label_b; //(max_label+1);
  label_list hist_label; //(max_label+1);
  float_list exponent_a; //(pixel_count);
  float_list exponent_b; //(pixel_count);
  float_list dst_list;
  RGB_colour* block_colour = block_colour_list.data_ptr;
  
  allocate_float(&dst_list,pixel_count*3);
  allocate_float(&exponent_a,pixel_count);
  allocate_float(&exponent_b,pixel_count);
  allocate_label(&hist_label_a,_max_label+1);
  allocate_label(&hist_label_b,hist_label_a.size);
  allocate_label(&hist_label,hist_label_a.size);
  
  p_float* dst_ptr = dst_list.data_ptr;
  p_float* interp_out_ptr = dst_ptr;
  
  p_float* exponent_a_ptr = exponent_a.data_ptr;
  p_float* exponent_b_ptr = exponent_b.data_ptr;
  
  blabel* min_dist_a_ptr = min_field.data_ptr1;
  blabel* min_dist_b_ptr = min_field.data_ptr3;
  blabel* label_a_ptr = min_field.data_ptr0;
  blabel* label_b_ptr = min_field.data_ptr2;
  blabel* label_ptr = block_label.data_ptr;
  
  label_histogram(label_a_ptr,&hist_label_a,pixel_count);
  label_histogram(label_b_ptr,&hist_label_b,pixel_count);
  label_histogram(label_ptr,&hist_label,pixel_count);
  
  blabel* lbl_a_ptr = label_a_ptr; // closest colour label
  blabel* lbl_b_ptr = label_b_ptr; // 2nd closest colour label
  blabel* lbl_ptr = label_ptr; // colour label
  p_float* exp_a_ptr = exponent_a_ptr;
  p_float* exp_b_ptr = exponent_b_ptr;
  blabel lbl_a = 0;
  blabel lbl_b = 0;
  blabel lbl = 0;
  
  for(size_t p = 0; p < pixel_count; p++) {
    lbl_a = hist_label_a.data_ptr[*lbl_a_ptr];
    lbl_b = hist_label_b.data_ptr[*lbl_b_ptr];
    lbl = hist_label.data_ptr[*lbl_ptr];
    
    *exp_a_ptr = 0.25f * (p_float)lbl_a / (p_float)lbl;
    *exp_b_ptr = 0.25f * (p_float)lbl_b / (p_float)lbl;
    
    if(*exp_a_ptr > 0.5f)
      *exp_a_ptr = 0.5f;
    
    if(*exp_b_ptr > 0.5f)
      *exp_b_ptr = 0.5f;
    
    exp_a_ptr++;
    exp_b_ptr++;
    lbl_a_ptr++;
    lbl_b_ptr++;
    lbl_ptr++;
  }
  size_t kernel_size = kern_size;
  filter_kernel exponent_kernel_2d;
  
  set_kernel_size(kernel_size, &exponent_kernel_2d);

  filter_exponent(frame_info->height,frame_info->width,&exponent_a,&exponent_kernel_2d);
  filter_exponent(frame_info->height,frame_info->width,&exponent_b,&exponent_kernel_2d);
  exp_a_ptr = exponent_a_ptr;
  exp_b_ptr = exponent_b_ptr;
  blabel* ma_ptr = min_dist_a_ptr;
  blabel* mb_ptr = min_dist_b_ptr;
  lbl_ptr = label_ptr;
  lbl_a_ptr = label_a_ptr;
  lbl_b_ptr = label_b_ptr;
  RGB_colour* colour_ptr;
  RGB_colour* colour_a_ptr;
  RGB_colour* colour_b_ptr;
  
  p_float* io_ptr = interp_out_ptr;
  
  for(size_t p = 0; p < pixel_count; p++) {
    lbl = *lbl_ptr;
    lbl_a = *lbl_a_ptr;
    lbl_b = *lbl_b_ptr;
    f_RGB_colour out_colour;
    p_float wght_alpha = 0.0f;
    p_float wght_beta = 1.0f;
    p_float dist_a = 0.0f;
    p_float dist_b = 0.0f;
    
    colour_ptr = &block_colour[lbl-1];
    colour_a_ptr = &block_colour[lbl_a-1];
    colour_b_ptr = NULL;
    
    p_float diff_a = colour_distance + 1.0f;
    p_float diff = 0.0f;
    
    if(*ma_ptr <= (blabel)spatial_distance) {
      p_float diff = (p_float)colour_ptr->r - (p_float)colour_a_ptr->r;
      diff *= diff;
      diff_a = diff;
      diff = (p_float)colour_ptr->g - (p_float)colour_a_ptr->g;
      diff *= diff;
      diff_a += diff;
      diff = (p_float)colour_ptr->b - (p_float)colour_a_ptr->b;
      diff *= diff;
      diff_a += diff;
    }
    
    int colour_amp = colour_ptr->r * colour_ptr->r + colour_ptr->g * colour_ptr->g + colour_ptr->b * colour_ptr->b;
    int colour_amp_a = colour_a_ptr->r * colour_a_ptr->r + colour_a_ptr->g * colour_a_ptr->g + colour_a_ptr->b * colour_a_ptr->b;
    
    bool low_amp = false;
    
    
    if(colour_amp < 1 || colour_amp_a < 1) {
      low_amp = true;
    }
    
    if(diff_a < (p_float)colour_distance && !low_amp) {
      dist_a = (p_float)*ma_ptr / spatial_dist_scale;      
      *exp_a_ptr = 0.5f / pow(dist_a,*exp_a_ptr);
      
      *exp_a_ptr += dither_strength*(rand_float() - 0.5f);
      
      if(*exp_a_ptr < 0.0f)
	*exp_a_ptr = 0.0f;
      
      if(*exp_a_ptr > 1.0f)
	*exp_a_ptr = 1.0f;
      
      wght_alpha = *exp_a_ptr;
      wght_beta = 1.0 - wght_alpha;
    }
    
    out_colour.r = wght_beta * (p_float)colour_ptr->r + wght_alpha * (p_float)colour_a_ptr->r;
    out_colour.g = wght_beta * (p_float)colour_ptr->g + wght_alpha * (p_float)colour_a_ptr->g;
    out_colour.b = wght_beta * (p_float)colour_ptr->b + wght_alpha * (p_float)colour_a_ptr->b;
    
    if(lbl_b > 0  && !low_amp) {
      p_float diff_b = colour_distance + 1.0f;
      colour_b_ptr = &block_colour[lbl_b-1];	
      
      int colour_amp_b = colour_b_ptr->r * colour_b_ptr->r + colour_b_ptr->g * colour_b_ptr->g + colour_b_ptr->b * colour_b_ptr->b;
      
      if(*mb_ptr <= spatial_distance && colour_amp_b > 1) {				
	diff = (p_float)colour_ptr->r - (p_float)colour_b_ptr->r;
	diff *= diff;
	diff_b = diff;
	diff = (p_float)colour_ptr->g - (p_float)colour_b_ptr->g;
	diff *= diff;
	diff_b += diff;
	diff = (p_float)colour_ptr->b - (p_float)colour_b_ptr->b;
	diff *= diff;
	diff_b += diff;
      }
      
    
      
      if(diff_b < colour_distance) {
	dist_b = (p_float)*mb_ptr / spatial_dist_scale;
	*exp_b_ptr = 0.5f / pow(dist_b,*exp_b_ptr);
	
	*exp_b_ptr += 0.1f*dither_strength*(rand_float() - 0.5f);
	
	if(*exp_b_ptr < 0.0f)
	  *exp_b_ptr = 0.0f;
	
	if(*exp_b_ptr > 1.0f)
	  *exp_b_ptr = 1.0f;
      
	wght_alpha = *exp_b_ptr;
	wght_beta = 1.0 - wght_alpha;
      }
      else {
	wght_alpha = 0.0f;
	wght_beta = 1.0f;
      }			
      
      out_colour.r = wght_beta * out_colour.r + wght_alpha * (p_float)colour_b_ptr->r;
      out_colour.g = wght_beta * out_colour.g + wght_alpha * (p_float)colour_b_ptr->g;
      out_colour.b = wght_beta * out_colour.b + wght_alpha * (p_float)colour_b_ptr->b;
    }
    else
      *exp_b_ptr = 0.0f;
    
    *(io_ptr) = out_colour.r;
    *(io_ptr+1) = out_colour.g;
    *(io_ptr+2) = out_colour.b;
        
    io_ptr += 3;
    exp_a_ptr++;
    exp_b_ptr++;
    ma_ptr++;
    mb_ptr++;
    lbl_a_ptr++;
    lbl_b_ptr++;
    lbl_ptr++;
  }
  
  // copy output
  dstrow = output_ptr;
  io_ptr = interp_out_ptr;
  p_float src[3];
  
  for (i = 0; i < frame_info->height; i++) {        
    uint8_t *dst = dstrow;
        
    for (j = 0; j < frame_info->width; j++) {
      offset = 3*j;
      src[0] = io_ptr[offset + 0] + 0.5f;
      src[1] = io_ptr[offset + 1] + 0.5f;
      src[2] = io_ptr[offset + 2] + 0.5f;
      
      if(src[0] > 255.0f) 
	src[0] = 255.0f;
      
      if(src[1] > 255.0f) 
	src[1] = 255.0f;
      
      if(src[2] > 255.0f) 
	src[2] = 255.0f;
      
      dst[offset + 0] = (pixel)src[0];
      dst[offset + 1] = (pixel)src[1];
      dst[offset + 2] = (pixel)src[2];      
    }
    
    dstrow += frame_info->dst_stride; 
    io_ptr += src_stride;
  }
  // interpolation
  
  // free allocations
  free_label(&block_label);
  free_pixel(&src_image);
  free_collection(&min_field);
  free_colour(&block_colour_list);
  free_label(&hist_label_a);
  free_label(&hist_label_b);
  free_label(&hist_label);
  free_float(&dst_list);
  free_float(&exponent_a);
  free_float(&exponent_b);
  free_kernel(&exponent_kernel_2d);  
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{    
    FlipContext *s = inlink->dst->priv;
    
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    int p, direct;

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;	
    } else {	
        direct = 0;
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }   
    
    size_t height = inlink->h;
    size_t width = inlink->w;
    
    uint8_t *dstrow;
    uint8_t *srcrow;
    int i, j;
    int offset;    
        
    FrameInfo frame_info;
    frame_info.width = width;
    frame_info.height = height;
    frame_info.src_stride = (int)in->linesize[0];
    frame_info.dst_stride = (int)out->linesize[0];
    
    dstrow = out->data[0];
    srcrow = in->data[0];
    deband_frame(dstrow,srcrow, &frame_info,s);
    
    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(FlipContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption deband_options[] = {
    { "colour_dist", "Defines radius for sphere of colours for interpolation.", OFFSET(colour_dist), AV_OPT_TYPE_FLOAT, { .dbl = 5.0 }, 0.0, 30.0, FLAGS },
    { "spatial_dist",   "Radius of local interpolation influence.",                          OFFSET(spatial_dist),   AV_OPT_TYPE_INT,   { .i64 = -1  }, -1,    40, FLAGS },
    { "dither_strength", "Dither strength", OFFSET(dither_strength), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0.0, 10.0, FLAGS },
    { "kernel_size",   "Exponent filter kernel size.",                          OFFSET(kernel_size),   AV_OPT_TYPE_INT,   { .i64 = -1  }, -1,    9, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(deband);

static const AVFilterPad avfilter_vf_deband_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
        .config_props     = config_input,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_deband_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_deband = {
    .name        = "deband",
    .description = NULL_IF_CONFIG_SMALL("Deband video using interpolation and dithering."),
    .priv_size   = sizeof(FlipContext),
    .priv_class    = &deband_class,
    .init          = init,
    .query_formats = query_formats,
    .inputs      = avfilter_vf_deband_inputs,
    .outputs     = avfilter_vf_deband_outputs,
};