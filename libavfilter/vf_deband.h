/*
 * Copyright (c) 2014 Gary Baugh baughg@tcd.ie
 * 
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

#ifndef VF_DEBAND_H
#define VF_DEBAND_H
typedef float p_float; 
typedef unsigned char pixel; 
typedef int blabel;
typedef short bool;

#define true 1
#define false 0

enum Forward{
  Forward_NV,
  Forward_LM,
  Forward_MU,
  Forward_LU,		
  Forward_RU,
  Forward_CP,
  Forward_NC	
};

enum ForwardNE {
  ForwardNE_NVNE,
  ForwardNE_NANE,
  ForwardNE_CA,
  ForwardNE_CB,
  ForwardNE_LMA,
  ForwardNE_MUA,
  ForwardNE_LUA,		
  ForwardNE_RUA,
  ForwardNE_LMB,
  ForwardNE_MUB,
  ForwardNE_LUB,		
  ForwardNE_RUB,		
  ForwardNE_NCNE		
};

enum Reverse {
  Reverse_NVR,
  Reverse_RM,
  Reverse_LB,
  Reverse_MB,
  Reverse_RB,
  Reverse_CPR,
  Reverse_NCR		
};

enum ReverseNE {
  ReverseNE_NVRNE,
  ReverseNE_NARNE,
  ReverseNE_CAR,
  ReverseNE_CBR,
  ReverseNE_RMA,
  ReverseNE_LBA,
  ReverseNE_MBA,
  ReverseNE_RBA,
  ReverseNE_RMB,
  ReverseNE_LBB,
  ReverseNE_MBB,
  ReverseNE_RBB,
  ReverseNE_NCRNE		
};


typedef struct {
  pixel r;
  pixel g;
  pixel b;
  pixel set;
} RGB_colour;

typedef struct {
  p_float r;
  p_float g;
  p_float b;	
} f_RGB_colour;

typedef struct {
  size_t width;
  size_t height;
  int src_stride;
  int dst_stride;
} FrameInfo;

typedef struct {
  RGB_colour* data_ptr;
  size_t size;
} rgb_colour_list;

typedef struct {
  blabel* data_ptr;
  size_t size;
}label_list;

typedef struct {
  p_float* data_ptr;
  size_t size;
}float_list;

typedef struct {
  blabel* data_ptr0;
  blabel* data_ptr1;
  blabel* data_ptr2;
  blabel* data_ptr3;
  size_t size;
}label_list_collection;

typedef struct {
  pixel* data_ptr;
  size_t size;
}pixel_list;

typedef struct {
  float_list kernel;
  label_list x_rel;
  label_list y_rel;
  size_t size;
} filter_kernel;



static void allocate_colour(rgb_colour_list* ptr, size_t size) {
  ptr->size = size;
  
  ptr->data_ptr = (RGB_colour*)av_malloc(size*sizeof(RGB_colour));
  memset((void*)ptr->data_ptr,0,size*sizeof(RGB_colour));
}

static void allocate_collection(label_list_collection *ptr, size_t size) {
  ptr->size = size;
  
  ptr->data_ptr0 = (blabel*)av_malloc(size*sizeof(blabel));
  memset((void*)ptr->data_ptr0,0,size*sizeof(blabel));
  
  ptr->data_ptr1 = (blabel*)av_malloc(size*sizeof(blabel));
  memset((void*)ptr->data_ptr1,0,size*sizeof(blabel));
  
  ptr->data_ptr2 = (blabel*)av_malloc(size*sizeof(blabel));
  memset((void*)ptr->data_ptr2,0,size*sizeof(blabel));
  
  ptr->data_ptr3 = (blabel*)av_malloc(size*sizeof(blabel));
  memset((void*)ptr->data_ptr3,0,size*sizeof(blabel));
}

static void allocate_label(label_list* ptr, size_t size) {
  ptr->data_ptr = (blabel*)av_malloc(size*sizeof(blabel));
  memset((void*)ptr->data_ptr,0,size*sizeof(blabel));
  ptr->size = size;
}

static void allocate_float(float_list* ptr, size_t size) {
  ptr->data_ptr = (p_float*)av_malloc(size*sizeof(p_float));
  memset((void*)ptr->data_ptr,0,size*sizeof(p_float));
  ptr->size = size;
}

static void allocate_pixel(pixel_list* ptr, size_t size) {
  ptr->data_ptr = (pixel*)av_malloc(size*sizeof(pixel));
  memset((void*)ptr->data_ptr,0,size*sizeof(pixel));
  ptr->size = size;
}

static void free_collection(label_list_collection *ptr) {
  av_free(ptr->data_ptr0);
  av_free(ptr->data_ptr1);
  av_free(ptr->data_ptr2);
  av_free(ptr->data_ptr3);
  ptr->size = 0;
}

static void free_label(label_list* ptr) {
  av_free(ptr->data_ptr);
  ptr->size = 0;
}

static void free_pixel(pixel_list* ptr) {
  av_free(ptr->data_ptr);
  ptr->size = 0;
}

static void free_colour(rgb_colour_list* ptr) {
  av_free(ptr->data_ptr);
  ptr->size = 0;
}

static void free_float(float_list* ptr) {
  av_free(ptr->data_ptr);
  ptr->size = 0;
}

static void allocate_kernel(filter_kernel* ptr, size_t size) {
  allocate_float(&ptr->kernel,size);
  allocate_label(&ptr->x_rel,size);
  allocate_label(&ptr->y_rel,size);
  ptr->size = size;
}

static void free_kernel(filter_kernel* ptr) {
  free_float(&ptr->kernel);
  free_label(&ptr->x_rel);
  free_label(&ptr->y_rel);
  ptr->size = 0;
}


static void label(pixel* rgb_ptr,const size_t height,const size_t width,label_list *block_label,size_t *_max_label);
static void label_forward(const label_list *block_init_label, const size_t blocks_per_row,const size_t blocks_per_column,label_list *block_label);
static void label_backward(const label_list *block_init_label, const size_t blocks_per_row,const size_t blocks_per_column,label_list *block_label);
static void label_distance(const size_t height,const size_t width,label_list *block_label,size_t max_label,label_list_collection *min_field);
static void label_stat(pixel*src_ptr,const size_t height,const size_t width,label_list *block_label,size_t _max_label,rgb_colour_list* block_colour_list);
static void label_histogram(blabel* label_ptr, label_list *hist,size_t pixel_count);
static void set_kernel_size(size_t kernel_size, filter_kernel* exponent_kernel_2d);
static void filter_exponent(const size_t height,const size_t width,float_list* exp_list, filter_kernel* exponent_kernel_2d);
#endif



