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

#include "libavutil/imgutils.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "vf_deband.h"
#include "limits.h"

static void print_label_field(const size_t height,const size_t width, label_list *label_ptr) {
  blabel* row_lbl_ptr = label_ptr->data_ptr;
  printf("field %dx%d\n",width,height);
  for(size_t y = 0; y < height; y++) {
    
    for(size_t x = 0; x < width; x++) {
      
      printf("%d,",*row_lbl_ptr);
      row_lbl_ptr++;
      
    }
    printf("NR\n");
  }
}

static void label(pixel* rgb_ptr,const size_t height,const size_t width,label_list *block_label,size_t *_max_label) {
  label_list init_label;
  size_t stride = width*3;
  allocate_label(&init_label,height*width);
 
  label_list _block_label;
  allocate_label(&_block_label,init_label.size);
  
  label_list _block_label_prev;
  allocate_label(&_block_label_prev,init_label.size);
  
  pixel* row_ptr = 0;
  
  blabel* lbl_ptr = init_label.data_ptr;
  blabel* row_lbl_ptr = 0;
  
  for(size_t y = 0; y < height; y++) {
    row_ptr = rgb_ptr + y*stride;
    row_lbl_ptr = lbl_ptr + y*width;
    
    for(size_t x = 0; x < width; x++) {
      *row_lbl_ptr = *row_ptr + 256*(*(row_ptr+1)) + 65536*(*(row_ptr+2));      
      row_lbl_ptr++;
      row_ptr += 3;
    }    
  }
  
  size_t block_label_size = _block_label.size;
  
  while(1) {
    
    label_forward(&init_label,width,height,&_block_label);    
    label_backward(&init_label,width,height,&_block_label);
   
    short different = 0;
    
    for(size_t p = 0; p < block_label_size; p++) {
      if(_block_label_prev.data_ptr[p] != _block_label.data_ptr[p])
      {
	different = 1;
	break;
      }
    }
    
    if(!different)
      break;
    
    memcpy(_block_label_prev.data_ptr,_block_label.data_ptr,_block_label_prev.size*sizeof(blabel));
  }
  
  blabel min_label = INT_MAX;
  blabel max_label = 0;
  
  blabel lbl = 0;
  
   
  for(size_t blk = 0; blk < block_label_size; blk++) {
    lbl = _block_label.data_ptr[blk];
    
    if(lbl > max_label)
      max_label = lbl;
    
    if(lbl < min_label)
      min_label = lbl;
  }  
    
  size_t label_count = max_label - min_label + 2;
  
  label_list label_mapping;
  allocate_label(&label_mapping,label_count);
  
  blabel current_label = 1;
  
  for(size_t blk = 0; blk < block_label_size; blk++) {
    lbl = _block_label.data_ptr[blk];
    
    if(label_mapping.data_ptr[lbl] == 0) {
      label_mapping.data_ptr[lbl] = current_label;
      current_label++;
    }
  }
  
  for(size_t blk = 0; blk < block_label_size; blk++) {
    lbl = _block_label.data_ptr[blk];
    
    block_label->data_ptr[blk] = label_mapping.data_ptr[lbl];
  }
  
  *_max_label = current_label-1;
    
  free_label(&init_label);  
  free_label(&_block_label);  
  free_label(&_block_label_prev);
  free_label(&label_mapping);
}

static void label_forward(const label_list *block_init_label, const size_t blocks_per_row,const size_t blocks_per_column,label_list *block_label) {
  const size_t block_count = block_init_label->size;
  
  if(block_label->size != block_count) {    
    allocate_label(block_label,block_count);
  }
  
  blabel label = 1;
  blabel* row_symbol_ptr = (blabel*)block_init_label->data_ptr;  
  blabel* block_label_ptr = (blabel*)block_label->data_ptr;
  
  block_label->data_ptr[0] = label;
  label++;
  
  blabel* symbol = row_symbol_ptr+1;
  
  // top row
  for(size_t x = 1; x < blocks_per_row; x++) {
    
    blabel lbl = block_label->data_ptr[x-1];
    
    if(*symbol == *(symbol-1) && lbl != 0) {
      block_label->data_ptr[x] = lbl;
    }
    else {				
      block_label->data_ptr[x] = label;
      label++;
    }
    
    
    symbol++;    
  }
  
  blabel* symbol_prev = row_symbol_ptr;
  symbol = row_symbol_ptr + blocks_per_row;
  
  // left column
  for(size_t y = 1; y < blocks_per_column; y++) {
    size_t index = y*blocks_per_row;
    
    
    blabel lbl = block_label->data_ptr[index-blocks_per_row];
    
    if(*symbol == *(symbol_prev) && lbl != 0) {
      block_label->data_ptr[index] = lbl;
    }
    else {				
      block_label->data_ptr[index] = label;
      label++;
    }
    
    symbol_prev = symbol;
    symbol += blocks_per_row;		
  }
  
  blabel* label_ptr = block_label_ptr;
  size_t blocks_per_row_lr = blocks_per_row-1;
  
  // body
  
  for(size_t y = 1; y < blocks_per_column; y++) {
    symbol = row_symbol_ptr + y*blocks_per_row + 1;    
    label_ptr = block_label_ptr + y*blocks_per_row + 1;
    
    for(size_t x = 1; x < blocks_per_row; x++) {
      
      // left
      blabel lbl = *(label_ptr - 1);
      bool assigned = false;
      bool increment = false;
      
      if(*symbol == *(symbol-1) && lbl != 0) {
	*label_ptr = lbl;
	assigned = true;
      }
      else {		
	assigned = true;
	increment = true;
	*label_ptr = label;	
      }
      
      // top
      lbl = *(label_ptr-blocks_per_row);
      
      if(*symbol == *(symbol-blocks_per_row) && lbl != 0) {
	if(*label_ptr == 0) {
	  *label_ptr = lbl;
	  assigned = true;
	}
	else if(lbl < *label_ptr) {
	  *label_ptr = lbl;
	  assigned = true;
	}
	
	increment = false;
      }
      else if(!assigned){		
	assigned = true;
	increment = true;
	*label_ptr = label;	
      }
      
      // top left
      lbl = *(label_ptr-blocks_per_row-1);
      
      if(*symbol == *(symbol-blocks_per_row-1) && lbl != 0) {
	if(*label_ptr == 0) {
	  *label_ptr = lbl;
	  assigned = true;
	}
	else if(lbl < *label_ptr) {
	  *label_ptr = lbl;
	  assigned = true;
	}
	
	increment = false;
      }
      else if(!assigned){	
	increment = true;
	*label_ptr = label;	
      }
      
      if(x < blocks_per_row_lr) {
	// top right
	lbl = *(label_ptr-blocks_per_row+1);
	
	if(*symbol == *(symbol-blocks_per_row+1) && lbl != 0) {
	  if(*label_ptr == 0) {
	    *label_ptr = lbl;
	    assigned = true;
	  }
	  else if(lbl < *label_ptr) {
	    *label_ptr = lbl;
	    assigned = true;
	  }
	  
	  increment = false;
	}
	else if(!assigned){	
	  increment = true;
	  *label_ptr = label;	  
	}
      }
      
      
      if(increment) {
	label++;
      }
      
      
      symbol++;      
      label_ptr++;
    }	
  }
}

static void label_backward(const label_list *block_init_label, const size_t blocks_per_row,const size_t blocks_per_column,label_list *block_label) {
  const size_t block_count = block_label->size;
  
  blabel* block_label_ptr = (blabel*)block_label->data_ptr;
  blabel* row_symbol_ptr = (blabel*)block_init_label->data_ptr;
  
  int offset = blocks_per_row*(blocks_per_column-2) + blocks_per_row - 1;
  
  blabel* label_ptr = block_label_ptr + offset;
  blabel* symbol = row_symbol_ptr + offset;
  
  // right column
  for(int y = blocks_per_column-2; y >= 0; y--) {
    
    blabel lbl = *(label_ptr + blocks_per_row);
    
    if(*symbol == *(symbol+blocks_per_row) && lbl != 0) {
      if(lbl < *label_ptr) {
	*label_ptr = lbl;
      }
    }
    
    lbl = *(label_ptr + blocks_per_row - 1);
    
    if(*symbol == *(symbol+blocks_per_row-1) && lbl != 0) {
      if(lbl < *label_ptr) {
	*label_ptr = lbl;
      }
    }
    
    label_ptr -= blocks_per_row;
    symbol -= blocks_per_row;		
  }
  
  // bottom row
  offset = blocks_per_row*(blocks_per_column-1) + blocks_per_row - 2;
  label_ptr = block_label_ptr + offset;
  symbol = row_symbol_ptr + offset;
   
  for(int x = blocks_per_row-2; x >= 0; x--) {
    
    blabel lbl = *(label_ptr + 1);
    
    if(*symbol == *(symbol+1) && lbl != 0) {
      if(lbl < *label_ptr)
	*label_ptr = lbl;
    }
    
    
    label_ptr--;
    symbol--;    
  }
  
  // body
  for(int y = blocks_per_column-2; y >= 0; y--) {
    offset = blocks_per_row*y + blocks_per_row - 2;
    symbol = row_symbol_ptr + offset;    
    label_ptr = block_label_ptr + offset;
    
    for(int x = blocks_per_row-2; x >= 0; x--) {
      // right
      int lbl = *(label_ptr + 1);
      
      if(*symbol == *(symbol+1) && lbl != 0) {
	if(lbl < *label_ptr)
	  *label_ptr = lbl;
      }
      // below
      lbl = *(label_ptr + blocks_per_row);
      
      if(*symbol == *(symbol+blocks_per_row) && lbl != 0) {
	if(lbl < *label_ptr)
	  *label_ptr = lbl;
      }
      // below right
      lbl = *(label_ptr + blocks_per_row + 1);
      
      if(*symbol == *(symbol+blocks_per_row+1) && lbl != 0) {
	if(lbl < *label_ptr)
	  *label_ptr = lbl;
      }
      
      if(x > 0) {
	// below left
	lbl = *(label_ptr + blocks_per_row - 1);
	
	if(*symbol == *(symbol+blocks_per_row-1) && lbl != 0) {
	  if(lbl < *label_ptr)
	    *label_ptr = lbl;
	}
      }
      
      symbol--;      
      label_ptr--;
    }
  }
}

static void label_distance(const size_t height,const size_t width,label_list *block_label,size_t max_label,label_list_collection *min_field) {
  const int D1 = 5;
  const int D2 = 7;
  
  const size_t MAX_LBL = width*height;
  size_t img_size = block_label->size;
  

  blabel* min_dist_a_ptr = min_field->data_ptr1;
  blabel* min_dist_b_ptr = min_field->data_ptr3;
  blabel* label_a_ptr = min_field->data_ptr0;
  blabel* label_b_ptr = min_field->data_ptr2;
  
  label_list guard_a; //(img_size);
  label_list guard_b; //(img_size);
  allocate_label(&guard_a,img_size);
  allocate_label(&guard_b,img_size);
  
  label_list label_change; //(img_size);
  allocate_label(&label_change,img_size);
  
  blabel* guard_a_ptr = guard_a.data_ptr;
  blabel* guard_b_ptr = guard_b.data_ptr;
  
  for(size_t p = 0; p < img_size; p++) {
    min_dist_a_ptr[p] = MAX_LBL;
    min_dist_b_ptr[p] = MAX_LBL;
    label_a_ptr[p] = 0;
    label_b_ptr[p] = 0;
  }
  
  blabel* block_label_ptr = block_label->data_ptr;
  blabel* pel_ptr = NULL;
  blabel* dist_ptr = NULL;
  blabel* guard_ptr = NULL;
  size_t offset = 0;
  size_t offset_upper = 0;
  const size_t width_end = width - 1;
  
  blabel local_label[Forward_NC];
  blabel local_dist[Forward_NC];
  blabel local_label_ne[ForwardNE_NCNE];
  blabel local_guard_ne[ForwardNE_NCNE];
  blabel local_dist_ne[ForwardNE_NCNE];
  
  while(1) {    
    // forward
    //y = 0
    {
      memset(local_label_ne,0,sizeof(local_label_ne));
      blabel* bl_ptr = block_label_ptr+1;		// init rgb label
      blabel* ma_ptr = min_dist_a_ptr+1; 
      blabel* la_ptr = label_a_ptr+1;		
      blabel* ga_ptr = guard_a_ptr+1;
      
      blabel* mb_ptr = min_dist_b_ptr+1;		
      blabel* lb_ptr = label_b_ptr+1;		
      blabel* gb_ptr = guard_b_ptr+1;
      
      for(size_t x = 1; x < width; x++) {
	local_label[Forward_CP] = *bl_ptr;
	local_dist[Forward_CP] = *ma_ptr;
	
	local_label[Forward_LM] = Forward_NV;
	local_label_ne[ForwardNE_LMA] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_LMB] = ForwardNE_NVNE;
	
	local_label_ne[ForwardNE_CA] = *la_ptr;
	local_dist_ne[ForwardNE_CA] = *ma_ptr;
	local_guard_ne[ForwardNE_CA] = *ga_ptr;
	local_label_ne[ForwardNE_CB] = *lb_ptr;
	local_dist_ne[ForwardNE_CB] = *mb_ptr;
	local_guard_ne[ForwardNE_CB] = *gb_ptr;
	
	// left
	pel_ptr = bl_ptr-1;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label[Forward_LM] = *pel_ptr;
	  local_dist[Forward_LM] = D1;
	}
	
	// left ocean a
	pel_ptr = la_ptr-1;
	dist_ptr = ma_ptr-1;
	guard_ptr = ga_ptr-1;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label_ne[ForwardNE_LMA] = *pel_ptr;
	  local_dist_ne[ForwardNE_LMA] = *dist_ptr + D1;
	  local_guard_ne[ForwardNE_LMA] = *guard_ptr;
	}
	
	// left ocean b
	pel_ptr = lb_ptr-1;
	dist_ptr = mb_ptr-1;
	guard_ptr = gb_ptr-1;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label_ne[ForwardNE_LMB] = *pel_ptr;
	  local_dist_ne[ForwardNE_LMB] = *dist_ptr + D1;
	  local_guard_ne[ForwardNE_LMB] = *guard_ptr;
	}
	
	// edge
	if(local_label[Forward_LM] != Forward_NV) {
	  local_label_ne[ForwardNE_NVNE] = local_label[Forward_LM];
	  local_dist_ne[ForwardNE_NVNE] = local_dist[Forward_LM];
	  local_guard_ne[ForwardNE_NVNE] = local_label[Forward_CP];
	}
	
	blabel min_d_a = INT_MAX;
	blabel min_d_b = INT_MAX;
	blabel _ga = 0;
	blabel _gb = 0;
	blabel _la = 0;
	blabel _lb = 0;
	
	for(size_t n = ForwardNE_NVNE; n <= ForwardNE_LMB; n++) {
	  if(local_label_ne[n] != ForwardNE_NVNE && local_guard_ne[n] == local_label[Forward_CP]) {
	    if(local_dist_ne[n] < min_d_a) {   
	      min_d_a = local_dist_ne[n];
	      _ga = local_guard_ne[n];
	      _la = local_label_ne[n];
	    }
	  }
	}
	
	// min b
	for(size_t n = ForwardNE_NVNE; n <= ForwardNE_RUB; n++) {
	  if(local_label_ne[n] != ForwardNE_NVNE && local_guard_ne[n] == local_label[Forward_CP]) {
	    if(local_dist_ne[n] < min_d_b && local_label_ne[n] != _la) {	      
	      min_d_b = local_dist_ne[n];
	      _gb = local_guard_ne[n];
	      _lb = local_label_ne[n];
	    }
	    
	  }
	}
	// check a & b
	*ma_ptr = min_d_a;
	*ga_ptr = _ga;
	*la_ptr = _la;
	
	*mb_ptr = min_d_b;
	*gb_ptr = _gb;
	*lb_ptr = _lb;
	
	bl_ptr++;			
	ga_ptr++;
	la_ptr++;
	ma_ptr++;
	gb_ptr++;
	lb_ptr++;
	mb_ptr++;
      }
    }
    
    // forward block y=1 to y=end
    for(size_t y = 1; y < height; y++) {
      offset = y*width;
      offset_upper = offset - width;
      
      blabel* bl_ptr = block_label_ptr + offset;
      blabel* blu_ptr = block_label_ptr + offset_upper;
      blabel* ma_ptr = min_dist_a_ptr + offset;
      blabel* mau_ptr = min_dist_a_ptr + offset_upper;
      blabel* la_ptr = label_a_ptr + offset;
      blabel* lau_ptr = label_a_ptr + offset_upper;
      blabel* ga_ptr = guard_a_ptr + offset;
      blabel* gau_ptr = guard_a_ptr + offset_upper;
      
      blabel* mb_ptr = min_dist_b_ptr + offset;
      blabel* mbu_ptr = min_dist_b_ptr + offset_upper;
      blabel* lb_ptr = label_b_ptr + offset;
      blabel* lbu_ptr = label_b_ptr + offset_upper;
      blabel* gb_ptr = guard_b_ptr + offset;
      blabel* gbu_ptr = guard_b_ptr + offset_upper;
      
      bool edge = false;
      blabel dir_sel = Forward_NV;
      
      for(size_t x = 0; x < width; x++) {
	local_label[Forward_CP] = *bl_ptr;
	local_dist[Forward_CP] = *ma_ptr;
	edge = false;
	// edge test
	local_label[Forward_LM] = Forward_NV;
	local_label[Forward_LU] = Forward_NV;
	local_label[Forward_RU] = Forward_NV;
	local_label[Forward_MU] = Forward_NV;
	
	local_label_ne[ForwardNE_CA] = *la_ptr;
	local_dist_ne[ForwardNE_CA] = *ma_ptr;
	local_guard_ne[ForwardNE_CA] = *ga_ptr;
	local_label_ne[ForwardNE_CB] = *lb_ptr;
	local_dist_ne[ForwardNE_CB] = *mb_ptr;
	local_guard_ne[ForwardNE_CB] = *gb_ptr;
	
	// ocean test
	local_label_ne[ForwardNE_NVNE] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_NANE] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_LMA] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_LUA] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_RUA] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_MUA] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_LMB] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_LUB] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_RUB] = ForwardNE_NVNE;
	local_label_ne[ForwardNE_MUB] = ForwardNE_NVNE;
	
	if(x > 0) {
	  // left
	  pel_ptr = bl_ptr-1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label[Forward_LM] = *pel_ptr;
	    local_dist[Forward_LM] = D1;
	    edge = true;
	  }
	  
	  pel_ptr = blu_ptr-1;
	  // left upper
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label[Forward_LU] = *pel_ptr;
	    local_dist[Forward_LU] = D2;
	    edge = true;
	  }	
	  
	  // left ocean a
	  pel_ptr = la_ptr-1;
	  dist_ptr = ma_ptr-1;
	  guard_ptr = ga_ptr-1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_LMA] = *pel_ptr;
	    local_dist_ne[ForwardNE_LMA] = *dist_ptr + D1;
	    local_guard_ne[ForwardNE_LMA] = *guard_ptr;
	  }
	  
	  // left ocean b
	  pel_ptr = lb_ptr-1;
	  dist_ptr = mb_ptr-1;
	  guard_ptr = gb_ptr-1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_LMB] = *pel_ptr;
	    local_dist_ne[ForwardNE_LMB] = *dist_ptr + D1;
	    local_guard_ne[ForwardNE_LMB] = *guard_ptr;
	  }
	  
	  // left upper ocean a
	  pel_ptr = lau_ptr-1;
	  dist_ptr = mau_ptr-1;
	  guard_ptr = gau_ptr-1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_LUA] = *pel_ptr;
	    local_dist_ne[ForwardNE_LUA] = *dist_ptr + D2;
	    local_guard_ne[ForwardNE_LUA] = *guard_ptr;
	  }
	  
	  // left upper ocean b
	  pel_ptr = lbu_ptr-1;
	  dist_ptr = mbu_ptr-1;
	  guard_ptr = gbu_ptr-1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_LUB] = *pel_ptr;
	    local_dist_ne[ForwardNE_LUB] = *dist_ptr + D2;
	    local_guard_ne[ForwardNE_LUB] = *guard_ptr;
	  }
	}
	
	if(x < width_end) {
	  // right upper
	  pel_ptr = blu_ptr+1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label[Forward_RU] = *pel_ptr;
	    local_dist[Forward_RU] = D2;
	    edge = true;
	  }	
	  
	  // right upper ocean a
	  pel_ptr = lau_ptr+1;
	  dist_ptr = mau_ptr+1;
	  guard_ptr = gau_ptr+1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_RUA] = *pel_ptr;
	    local_dist_ne[ForwardNE_RUA] = *dist_ptr + D2;
	    local_guard_ne[ForwardNE_RUA] = *guard_ptr;
	  }
	  
	  // right upper ocean b
	  pel_ptr = lbu_ptr+1;
	  dist_ptr = mbu_ptr+1;
	  guard_ptr = gbu_ptr+1;
	  
	  if(*pel_ptr != local_label[Forward_CP]) {
	    local_label_ne[ForwardNE_RUB] = *pel_ptr;
	    local_dist_ne[ForwardNE_RUB] = *dist_ptr + D2;
	    local_guard_ne[ForwardNE_RUB] = *guard_ptr;
	  }
	}
	
	// upper
	pel_ptr = blu_ptr;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label[Forward_MU] = *pel_ptr;
	  local_dist[Forward_MU] = D1;
	  edge = true;
	}
	
	// upper ocean a
	pel_ptr = lau_ptr;
	dist_ptr = mau_ptr;
	guard_ptr = gau_ptr;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label_ne[ForwardNE_MUA] = *pel_ptr;
	  local_dist_ne[ForwardNE_MUA] = *dist_ptr + D1;
	  local_guard_ne[ForwardNE_MUA] = *guard_ptr;
	}
	
	// upper ocean b
	pel_ptr = lbu_ptr;
	dist_ptr = mbu_ptr;
	guard_ptr = gbu_ptr;
	
	if(*pel_ptr != local_label[Forward_CP]) {
	  local_label_ne[ForwardNE_MUB] = *pel_ptr;
	  local_dist_ne[ForwardNE_MUB] = *dist_ptr + D1;
	  local_guard_ne[ForwardNE_MUB] = *guard_ptr;
	}
	
	if(edge) {
	  dir_sel = Forward_NV;
	  
	  if(local_label[Forward_LM] != Forward_NV) {
	    local_dist_ne[ForwardNE_NVNE] = local_dist[Forward_LM]; 						
	    local_label_ne[ForwardNE_NVNE] = local_label[Forward_LM];
	    dir_sel = Forward_LM;
	  }
	  else if(local_label[Forward_MU] != Forward_NV) {
	    local_dist_ne[ForwardNE_NVNE] = local_dist[Forward_MU]; 	
	    local_label_ne[ForwardNE_NVNE] = local_label[Forward_MU];
	    dir_sel = Forward_MU;
	  }
	  else if(local_label[Forward_LU] != Forward_NV) {
	    local_dist_ne[ForwardNE_NVNE] = local_dist[Forward_LU]; 	
	    local_label_ne[ForwardNE_NVNE] = local_label[Forward_LU];
	    dir_sel = Forward_LU;
	  }
	  else if(local_label[Forward_RU] != Forward_NV) {
	    local_dist_ne[ForwardNE_NVNE] = local_dist[Forward_RU]; 	
	    local_label_ne[ForwardNE_NVNE] = local_label[Forward_RU];
	    dir_sel = Forward_RU;
	  }
	  
	  local_guard_ne[ForwardNE_NVNE] = local_label[Forward_CP];
	  
	  if(local_label[Forward_LM] != Forward_NV && local_label[dir_sel] != local_label[Forward_LM]) {
	    local_dist_ne[ForwardNE_NANE] = local_dist[Forward_LM]; 	
	    local_label_ne[ForwardNE_NANE] = local_label[Forward_LM];	
	    local_guard_ne[ForwardNE_NANE] = local_label[Forward_CP];
	  }
	  else if(local_label[Forward_MU] != Forward_NV && local_label[dir_sel] != local_label[Forward_MU]) {
	    local_dist_ne[ForwardNE_NANE] = local_dist[Forward_MU]; 	
	    local_label_ne[ForwardNE_NANE] = local_label[Forward_MU];		
	    local_guard_ne[ForwardNE_NANE] = local_label[Forward_CP];
	  }
	  else if(local_label[Forward_LU] != Forward_NV && local_label[dir_sel] != local_label[Forward_LU]) {
	    local_dist_ne[ForwardNE_NANE] = local_dist[Forward_LU]; 	
	    local_label_ne[ForwardNE_NANE] = local_label[Forward_LU];	
	    local_guard_ne[ForwardNE_NANE] = local_label[Forward_CP];
	  }
	  else if(local_label[Forward_RU] != Forward_NV && local_label[dir_sel] != local_label[Forward_RU]) {
	    local_dist_ne[ForwardNE_NANE] = local_dist[Forward_RU]; 	
	    local_label_ne[ForwardNE_NANE] = local_label[Forward_RU];	
	    local_guard_ne[ForwardNE_NANE] = local_label[Forward_CP];
	  }
	}
	// edge test
	
	// ocean test
	blabel min_d_a = INT_MAX;
	blabel min_d_b = INT_MAX;
	blabel _ga = 0;
	blabel _gb = 0;
	blabel _la = 0;
	blabel _lb = 0;
	
	for(size_t n = ForwardNE_NVNE; n <= ForwardNE_RUB; n++) {
	  if(local_label_ne[n] != ForwardNE_NVNE && local_guard_ne[n] == local_label[Forward_CP]) {
	    if(local_dist_ne[n] < min_d_a) {
	      min_d_a = local_dist_ne[n];
	      _ga = local_guard_ne[n];
	      _la = local_label_ne[n];
	    }
	  }
	}
	
	// min b
	for(size_t n = ForwardNE_NVNE; n <= ForwardNE_RUB; n++) {
	  if(local_label_ne[n] != ForwardNE_NVNE && local_guard_ne[n] == local_label[Forward_CP]) {
	    if(local_dist_ne[n] < min_d_b && local_label_ne[n] != _la) {
	      min_d_b = local_dist_ne[n];
	      _gb = local_guard_ne[n];
	      _lb = local_label_ne[n];
	    }
	  }
	}
	// check a & b
	*ma_ptr = min_d_a;
	*ga_ptr = _ga;
	*la_ptr = _la;
	
	*mb_ptr = min_d_b;
	*gb_ptr = _gb;
	*lb_ptr = _lb;
	
	bl_ptr++;			
	ga_ptr++;
	la_ptr++;
	ma_ptr++;
	gb_ptr++;
	lb_ptr++;
	mb_ptr++;
	
	//upper
	blu_ptr++;			
	gau_ptr++;
	lau_ptr++;
	mau_ptr++;
	gbu_ptr++;
	lbu_ptr++;
	mbu_ptr++;
	
      }
    }
    // forward block y=1 to y=end
    
    // reverse
    // y = height-1
    {
      //local_label_ne[ReverseNE_NVRNE] = ReverseNE_NVRNE;
      memset(local_label_ne,0,sizeof(local_label_ne));
      local_label_ne[ReverseNE_RMB] = ReverseNE_NVRNE;
      
      size_t rev_offset = height*width - 2;
      
      blabel* bl_ptr = block_label_ptr+rev_offset;		// init rgb label
      blabel* ma_ptr = min_dist_a_ptr+rev_offset; 
      blabel* la_ptr = label_a_ptr+rev_offset;		
      blabel* ga_ptr = guard_a_ptr+rev_offset;
      
      blabel* mb_ptr = min_dist_b_ptr+rev_offset;		
      blabel* lb_ptr = label_b_ptr+rev_offset;		
      blabel* gb_ptr = guard_b_ptr+rev_offset;
      
      for(int x = width_end; x >= 0; x--) {
	local_label[Reverse_CPR] = *bl_ptr;
	
	local_label[Reverse_RM] = Reverse_NVR;
	local_label_ne[ReverseNE_RMA] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_RMB] = ReverseNE_NVRNE;
	
	local_label_ne[ReverseNE_CAR] = *la_ptr;
	local_dist_ne[ReverseNE_CAR] = *ma_ptr;
	local_guard_ne[ReverseNE_CAR] = *ga_ptr;
	local_label_ne[ReverseNE_CBR] = *lb_ptr;
	local_dist_ne[ReverseNE_CBR] = *mb_ptr;
	local_guard_ne[ReverseNE_CBR] = *gb_ptr;
	
	// right
	pel_ptr = bl_ptr+1;
	
	if(*pel_ptr != local_label[Reverse_CPR]) {
	  local_label[Reverse_RM] = *pel_ptr;
	  local_dist[Reverse_RM] = D1;
	}
	
	// right ocean a
	pel_ptr = la_ptr+1;
	dist_ptr = ma_ptr+1;
	guard_ptr = ga_ptr+1;
	
	if(*pel_ptr != local_label[Reverse_CPR]) {
	  local_label_ne[ReverseNE_RMA] = *pel_ptr;
	  local_dist_ne[ReverseNE_RMA] = *dist_ptr + D1;
	  local_guard_ne[ReverseNE_RMA] = *guard_ptr;
	}
	
	// right ocean b
	pel_ptr = lb_ptr+1;
	dist_ptr = mb_ptr+1;
	guard_ptr = gb_ptr+1;
	
	if(*pel_ptr != local_label[Reverse_CPR]) {
	  local_label_ne[ReverseNE_RMB] = *pel_ptr;
	  local_dist_ne[ReverseNE_RMB] = *dist_ptr + D1;
	  local_guard_ne[ReverseNE_RMB] = *guard_ptr;
	}
	
	// edge
	if(local_label[Reverse_RM] != Reverse_NVR) {
	  local_label_ne[ReverseNE_NVRNE] = local_label[Reverse_RM];
	  local_dist_ne[ReverseNE_NVRNE] = local_dist[Reverse_RM];
	  local_guard_ne[ReverseNE_NVRNE] = local_label[Reverse_CPR];
	}
	
	// check ocean
	blabel min_d_a = INT_MAX;			
	blabel _ga = 0;	
	blabel _la = 0;
	blabel min_d_b = INT_MAX;			
	blabel _gb = 0;	
	blabel _lb = 0;
	
	for(size_t n = ReverseNE_NVRNE; n <= ReverseNE_RMB; n++) {
	  if(local_label_ne[n] != ReverseNE_NVRNE && local_guard_ne[n] == local_label[Reverse_CPR]) {
	    if(local_dist_ne[n] < min_d_a) {
	      min_d_a = local_dist_ne[n];
	      _ga = local_guard_ne[n];
	      _la = local_label_ne[n];
	    }
	  }
	}
	
	// min b
	for(size_t n = ReverseNE_NVRNE; n <= ReverseNE_RBB; n++) {
	  if(local_label_ne[n] != ReverseNE_NVRNE && local_guard_ne[n] == local_label[Reverse_CPR]) {
	    if(local_dist_ne[n] < min_d_b && local_label_ne[n] != _la) {
	      min_d_b = local_dist_ne[n];
	      _gb = local_guard_ne[n];
	      _lb = local_label_ne[n];
	    }
	  }
	}
	// check a & b
	
	*ma_ptr = min_d_a;
	*ga_ptr = _ga;
	*la_ptr = _la;
	*mb_ptr = min_d_b;
	*gb_ptr = _gb;
	*lb_ptr = _lb;
	
	
	bl_ptr--;
	ma_ptr--;
	la_ptr--;
	ga_ptr--;
	mb_ptr--;
	lb_ptr--;
	gb_ptr--;
      }
    }
    // y = height-2 -> y=0
    for(int y = height-2; y >= 0; y--) {
      offset = y*width + width_end;
      offset_upper = offset + width;
      
      blabel* bl_ptr = block_label_ptr + offset;
      blabel* blb_ptr = block_label_ptr + offset_upper;
      blabel* ma_ptr = min_dist_a_ptr + offset;
      blabel* mab_ptr = min_dist_a_ptr + offset_upper;
      blabel* la_ptr = label_a_ptr + offset;
      blabel* lab_ptr = label_a_ptr + offset_upper;
      blabel* ga_ptr = guard_a_ptr + offset;
      blabel* gab_ptr = guard_a_ptr + offset_upper;
      
      blabel* mb_ptr = min_dist_b_ptr + offset;
      blabel* mbb_ptr = min_dist_b_ptr + offset_upper;
      blabel* lb_ptr = label_b_ptr + offset;
      blabel* lbb_ptr = label_b_ptr + offset_upper;
      blabel* gb_ptr = guard_b_ptr + offset;
      blabel* gbb_ptr = guard_b_ptr + offset_upper;
      bool edge = false;
      blabel dir_sel = Reverse_NVR;
      
      for(int x = width_end; x >= 0; x--) {
	local_label[Reverse_CPR] = *bl_ptr;
	
	local_label[Reverse_RM] = Reverse_NVR;
	local_label[Reverse_LB] = Reverse_NVR;
	local_label[Reverse_MB] = Reverse_NVR;
	local_label[Reverse_RB] = Reverse_NVR;
	
	local_label_ne[ReverseNE_CAR] = *la_ptr;
	local_dist_ne[ReverseNE_CAR] = *ma_ptr;
	local_guard_ne[ReverseNE_CAR] = *ga_ptr;
	local_label_ne[ReverseNE_CBR] = *lb_ptr;
	local_dist_ne[ReverseNE_CBR] = *mb_ptr;
	local_guard_ne[ReverseNE_CBR] = *gb_ptr;
	
	local_label_ne[ReverseNE_NVRNE] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_NARNE] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_RMA] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_LBA] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_MBA] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_RBA] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_RMB] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_LBB] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_MBB] = ReverseNE_NVRNE;
	local_label_ne[ReverseNE_RBB] = ReverseNE_NVRNE;
	
	edge = false;
	// left 
	if(x > 0) {
	  // bottom left
	  pel_ptr = blb_ptr - 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) { 
	    local_label[Reverse_LB] = *pel_ptr;
	    local_dist[Reverse_LB] = D2;
	    edge = true;
	  }
	  
	  // bottom left ocean a
	  pel_ptr = lab_ptr - 1;
	  dist_ptr = mab_ptr - 1;
	  guard_ptr = gab_ptr - 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_LBA] = *pel_ptr;
	    local_dist_ne[ReverseNE_LBA] = *dist_ptr + D2;
	    local_guard_ne[ReverseNE_LBA] = *guard_ptr;
	  }
	  
	  // bottom left ocean b
	  pel_ptr = lbb_ptr - 1;
	  dist_ptr = mbb_ptr - 1;
	  guard_ptr = gbb_ptr - 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_LBB] = *pel_ptr;
	    local_dist_ne[ReverseNE_LBB] = *dist_ptr + D2;
	    local_guard_ne[ReverseNE_LBB] = *guard_ptr;
	  }
	}
	
	// right
	if(x < width_end) {
	  // right 
	  pel_ptr = bl_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) { 
	    local_label[Reverse_RM] = *pel_ptr;
	    local_dist[Reverse_RM] = D1;
	    edge = true;
	  }
	  
	  // right bottom
	  pel_ptr = blb_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) { 
	    local_label[Reverse_LB] = *pel_ptr;
	    local_dist[Reverse_LB] = D2;
	    edge = true;
	  }
	  
	  // right ocean a
	  pel_ptr = la_ptr + 1;
	  dist_ptr = ma_ptr + 1;
	  guard_ptr = ga_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_RMA] = *pel_ptr;
	    local_dist_ne[ReverseNE_RMA] = *dist_ptr + D1;
	    local_guard_ne[ReverseNE_RMA] = *guard_ptr;
	  }
	  
	  // right ocean b
	  pel_ptr = lb_ptr + 1;
	  dist_ptr = mb_ptr + 1;
	  guard_ptr = gb_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_RMB] = *pel_ptr;
	    local_dist_ne[ReverseNE_RMB] = *dist_ptr + D1;
	    local_guard_ne[ReverseNE_RMB] = *guard_ptr;
	  }
	  
	  
	  // right bottom ocean a
	  pel_ptr = lab_ptr + 1;
	  dist_ptr = mab_ptr + 1;
	  guard_ptr = gab_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_RBA] = *pel_ptr;
	    local_dist_ne[ReverseNE_RBA] = *dist_ptr + D2;
	    local_guard_ne[ReverseNE_RBA] = *guard_ptr;
	  }
	  
	  // right bottom ocean b
	  pel_ptr = lbb_ptr + 1;
	  dist_ptr = mbb_ptr + 1;
	  guard_ptr = gbb_ptr + 1;
	  
	  if(*pel_ptr != local_label[Reverse_CPR]) {
	    local_label_ne[ReverseNE_RBB] = *pel_ptr;
	    local_dist_ne[ReverseNE_RBB] = *dist_ptr + D2;
	    local_guard_ne[ReverseNE_RBB] = *guard_ptr;
	  }
	}
	
	// below
	pel_ptr = blb_ptr;
	
	if(*pel_ptr != local_label[Reverse_CPR]) { 
	  local_label[Reverse_MB] = *pel_ptr;
	  local_dist[Reverse_MB] = D1;
	  edge = true;
	}
	
	// below ocean a
	pel_ptr = lab_ptr;
	dist_ptr = mab_ptr;
	guard_ptr = gab_ptr;
	
	if(*pel_ptr != local_label[Reverse_CPR]) {
	  local_label_ne[ReverseNE_MBA] = *pel_ptr;
	  local_dist_ne[ReverseNE_MBA] = *dist_ptr + D1;
	  local_guard_ne[ReverseNE_MBA] = *guard_ptr;
	}
	
	// below ocean b
	pel_ptr = lbb_ptr;
	dist_ptr = mbb_ptr;
	guard_ptr = gbb_ptr;
	
	if(*pel_ptr != local_label[Reverse_CPR]) {
	  local_label_ne[ReverseNE_MBB] = *pel_ptr;
	  local_dist_ne[ReverseNE_MBB] = *dist_ptr + D1;
	  local_guard_ne[ReverseNE_MBB] = *guard_ptr;
	}
	
	if(edge) {
	  dir_sel = Reverse_NVR;
	  
	  if(local_label[Reverse_RM] != Reverse_NVR) {
	    local_dist_ne[ReverseNE_NVRNE] = local_dist[Reverse_RM]; 						
	    local_label_ne[ReverseNE_NVRNE] = local_label[Reverse_RM];
	    dir_sel = Reverse_RM;
	  }
	  else if(local_label[Reverse_MB] != Reverse_NVR) {
	    local_dist_ne[ReverseNE_NVRNE] = local_dist[Reverse_MB]; 	
	    local_label_ne[ReverseNE_NVRNE] = local_label[Reverse_MB];
	    dir_sel = Reverse_MB;
	  }
	  else if(local_label[Reverse_LB] != Reverse_NVR) {
	    local_dist_ne[ReverseNE_NVRNE] = local_dist[Reverse_LB]; 	
	    local_label_ne[ReverseNE_NVRNE] = local_label[Reverse_LB];
	    dir_sel = Reverse_LB;
	  }
	  else if(local_label[Reverse_RB] != Reverse_NVR) {
	    local_dist_ne[ReverseNE_NVRNE] = local_dist[Reverse_RB]; 	
	    local_label_ne[ReverseNE_NVRNE] = local_label[Reverse_RB];
	    dir_sel = Reverse_RB;
	  }
	  
	  local_guard_ne[ReverseNE_NVRNE] = local_label[Reverse_CPR];
	  
	  if(local_label[Reverse_RM] != Reverse_NVR && local_label[dir_sel] != local_label[Reverse_RM]) {
	    local_dist_ne[ReverseNE_NARNE] = local_dist[Reverse_RM]; 	
	    local_label_ne[ReverseNE_NARNE] = local_label[Reverse_RM];	
	    local_guard_ne[ReverseNE_NARNE] = local_label[Reverse_CPR];
	  }
	  else if(local_label[Reverse_MB] != Reverse_NVR && local_label[dir_sel] != local_label[Reverse_MB]) {
	    local_dist_ne[ReverseNE_NARNE] = local_dist[Reverse_MB]; 	
	    local_label_ne[ReverseNE_NARNE] = local_label[Reverse_MB];		
	    local_guard_ne[ReverseNE_NARNE] = local_label[Reverse_CPR];
	  }
	  else if(local_label[Reverse_LB] != Reverse_NVR && local_label[dir_sel] != local_label[Reverse_LB]) {
	    local_dist_ne[ReverseNE_NARNE] = local_dist[Reverse_LB]; 	
	    local_label_ne[ReverseNE_NARNE] = local_label[Reverse_LB];	
	    local_guard_ne[ReverseNE_NARNE] = local_label[Reverse_CPR];
	  }
	  else if(local_label[Reverse_RB] != Reverse_NVR && local_label[dir_sel] != local_label[Reverse_RB]) {
	    local_dist_ne[ReverseNE_NARNE] = local_dist[Reverse_RB]; 	
	    local_label_ne[ReverseNE_NARNE] = local_label[Reverse_RB];	
	    local_guard_ne[ReverseNE_NARNE] = local_label[Reverse_CPR];
	  }
	}
	
	// ocean test
	blabel min_d_a = INT_MAX;
	blabel min_d_b = INT_MAX;
	blabel _ga = 0;
	blabel _gb = 0;
	blabel _la = 0;
	blabel _lb = 0;
	
	for(size_t n = ReverseNE_NVRNE; n <= ReverseNE_RBB; n++) {
	  if(local_label_ne[n] != ReverseNE_NVRNE && local_guard_ne[n] == local_label[Reverse_CPR]) {
	    if(local_dist_ne[n] < min_d_a) {	      
	      min_d_a = local_dist_ne[n];
	      _ga = local_guard_ne[n];
	      _la = local_label_ne[n];
	    }
	  }
	}
	
	// min b
	for(size_t n = ReverseNE_NVRNE; n <= ReverseNE_RBB; n++) {
	  if(local_label_ne[n] != ReverseNE_NVRNE && local_guard_ne[n] == local_label[Reverse_CPR]) {
	    if(local_dist_ne[n] < min_d_b && local_label_ne[n] != _la) {
	      min_d_b = local_dist_ne[n];
	      _gb = local_guard_ne[n];
	      _lb = local_label_ne[n];
	    }
	  }
	}
	// check a & b
	
	*ma_ptr = min_d_a;
	*ga_ptr = _ga;
	*la_ptr = _la;
	*mb_ptr = min_d_b;
	*gb_ptr = _gb;
	*lb_ptr = _lb;

	bl_ptr--;
	blb_ptr--;
	ma_ptr--;
	mab_ptr--;
	la_ptr--;
	lab_ptr--;
	ga_ptr--;
	gab_ptr--;
	mb_ptr--;
	mbb_ptr--;
	lb_ptr--;
	lbb_ptr--;
	gb_ptr--;
	gbb_ptr--;
      }
    }
    
    bool different = false;
    
    for(size_t p = 0; p < img_size; p++) {
      if(label_change.data_ptr[p] != label_b_ptr[p]) {
	different = true;
	break;
      }
    }
    
    if(!different)
      break;
    
    memcpy(label_change.data_ptr,label_b_ptr,img_size*sizeof(blabel));
  }
  
  // free allocations
  free_label(&guard_a);
  free_label(&guard_b);
  free_label(&label_change);
}

static void label_stat(pixel* rgb_ptr,const size_t height,const size_t width,label_list *block_label,size_t _max_label,rgb_colour_list* block_colour_list) {
  RGB_colour* block_colour = block_colour_list->data_ptr;
  
  for(size_t b = 0; b < _max_label; b++) {
    block_colour[b].set = 0;
  }
  
  size_t block_label_size = block_colour_list->size;
  blabel* lbl_ptr = 0;
  blabel* top_ptr = block_label->data_ptr;
  pixel* row_ptr = 0;
  blabel lbl = 0;
  size_t stride = 3*width;
  
  for(size_t y = 0; y < height; y++) {
    lbl_ptr = top_ptr + y*width;
    row_ptr = rgb_ptr + y*stride;
    
    for(size_t x = 0; x < width; x++) {
      lbl = *lbl_ptr - 1;
      
      if(block_colour[lbl].set == 0) {
	block_colour[lbl].r = *row_ptr;
	block_colour[lbl].g = *(row_ptr+1);
	block_colour[lbl].b = *(row_ptr+2);
	block_colour[lbl].set = 1;
      }      
      
      lbl_ptr++;
      row_ptr += 3;
    }
  }
}

static void label_histogram(blabel* label_ptr, label_list *hist,size_t pixel_count) {	
  blabel* lbl_ptr = label_ptr;
  
  for(size_t p = 0; p < pixel_count; p++) {
    hist->data_ptr[*lbl_ptr]++;
    lbl_ptr++;
  }
}

static void set_kernel_size(size_t kernel_size, filter_kernel* exponent_kernel_2d) {
  if(kernel_size < 3) {
    kernel_size = 3;
  }
  
  if(kernel_size%2 == 0)
    kernel_size++;
  
  float_list exponent_kernel;
  allocate_float(&exponent_kernel,kernel_size);
  
  allocate_kernel(exponent_kernel_2d,kernel_size*kernel_size);
  
    
  size_t mid_point = (kernel_size+1)/2;
  int v = 1;
  bool slope_up = true;
  
  for(size_t k = 0; k < kernel_size; k++) {
    exponent_kernel.data_ptr[k] = v;
    
    if(v == mid_point) {
      slope_up = false;
    }
    
    if(slope_up) {
      v++;
    }
    else {
      v--;
    }
  }
  
  p_float* exp_ptr = exponent_kernel_2d->kernel.data_ptr;
  blabel* x_ptr = exponent_kernel_2d->x_rel.data_ptr;
  blabel* y_ptr = exponent_kernel_2d->y_rel.data_ptr;
  
  for(size_t col = 0; col < kernel_size; col++) {
    for(size_t k = 0; k < kernel_size; k++) {
      *exp_ptr = exponent_kernel.data_ptr[k]*exponent_kernel.data_ptr[col];
      *x_ptr = k - mid_point + 1;
      *y_ptr = col - mid_point + 1;
      
      exp_ptr++;
      x_ptr++;
      y_ptr++;
    }
  }
  
  p_float sum = 0.0f;
  
  for(size_t k = 0; k < exponent_kernel_2d->size; k++) {
    sum += exponent_kernel_2d->kernel.data_ptr[k];
  }
  
  for(size_t k = 0; k < exponent_kernel_2d->size; k++) {
    exponent_kernel_2d->kernel.data_ptr[k] /= sum;
  }
  // free allocations
  free_float(&exponent_kernel);
}

static void filter_exponent(const size_t height,const size_t width,float_list* exp_list, filter_kernel* exponent_kernel_2d) {
  p_float* exp_ptr = exp_list->data_ptr;
  
  float_list temp_buffer; 
  
  allocate_float(&temp_buffer,exp_list->size);
  p_float* out_ptr = temp_buffer.data_ptr;
  
  size_t exponent_kernel_2d_size = exponent_kernel_2d->size;
  p_float* ek_ptr = exponent_kernel_2d->kernel.data_ptr;
  blabel* xd_ptr = exponent_kernel_2d->x_rel.data_ptr;
  blabel* yd_ptr = exponent_kernel_2d->y_rel.data_ptr;
  int xr = 0;
  int yr = 0;
  size_t height_end = height - 1;
  size_t width_end = width - 1;
  p_float wght = 0.0f;
  
  for(size_t k = 0; k < exponent_kernel_2d_size; k++) {    
    wght = *ek_ptr;
    
    for(size_t y = 0; y < height; y++) {
      p_float* o_ptr = out_ptr + y*width;
      
      yr = y + *yd_ptr;
      
      if(yr < 0) {
	yr = 0;
      }
      
      if(yr > height_end) {
	yr = height_end;
      }
      
      p_float* e_ptr = exp_ptr + yr*width;
      
      for(size_t x = 0; x < width; x++) {
	xr = x + *xd_ptr;
	
	if(xr < 0) {
	  xr = 0;
	}
	
	if(xr > width_end) {
	  xr = width_end;
	}
	
	*o_ptr += e_ptr[xr]*wght;
	o_ptr++;
      }
    }
    
    xd_ptr++;
    yd_ptr++;
    ek_ptr++;
  }
  
  memcpy(exp_ptr,out_ptr,temp_buffer.size*sizeof(p_float));
  
  free_float(&temp_buffer);
}

