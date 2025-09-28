// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

// static const char
// rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#ifdef USE_VECTOR
    #include "riscv_vector.h"
#endif

//#define CMAP256

struct fb_var_screeninfo fb = {};
int fb_scaling = 1;
int usemouse = 0;

#ifndef USE_VECTOR
struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

static struct color colors[256];
#endif

// Vector palette - split into upper and lower 8-bit channels
#ifdef USE_VECTOR
static uint8_t vpalette_upper[256];
static uint8_t vpalette_lower[256];
#endif

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
byte *I_VideoBuffer_FB = NULL;

#ifdef USE_VECTOR
// Intermediate buffer for vector scaling - fixed 320x200 size
byte *I_VideoBuffer_Intermediate = NULL;
#endif

/* framebuffer file descriptor */
int fd_fb = 0;

int	X_width;
int X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// If true, we display dots at the bottom of the screen to 
// indicate FPS.

static boolean display_fps_dots;


// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

#ifndef USE_VECTOR
// Palette converted to RGB565

static uint16_t rgb565_palette[256];

void cmap_to_rgb565(uint16_t * out, uint8_t * in, int in_pixels)
{
    int i, j;
    struct color c;
    uint16_t r, g, b;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in]; 
        r = ((uint16_t)(c.r >> 3)) << 11;
        g = ((uint16_t)(c.g >> 2)) << 5;
        b = ((uint16_t)(c.b >> 3)) << 0;
        *out = (r | g | b);

        in++;
        for (j = 0; j < fb_scaling; j++) {
            out++;
        }
    }
}

void cmap_to_fb(uint8_t * out, uint8_t * in, int in_pixels)
{
    int i, j, k;
    struct color c;
    uint32_t pix;
    uint16_t r, g, b;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in];  /* R:8 G:8 B:8 format! */
        r = (uint16_t)(c.r >> (8 - fb.red.length));
        g = (uint16_t)(c.g >> (8 - fb.green.length));
        b = (uint16_t)(c.b >> (8 - fb.blue.length));
        pix = r << fb.red.offset;
        pix |= g << fb.green.offset;
        pix |= b << fb.blue.offset;

        for (k = 0; k < fb_scaling; k++) {
            for (j = 0; j < fb.bits_per_pixel/8; j++) {
                *out = (pix >> (j*8));
                out++;
            }
        }
        in++;
    }
}
#else
void draw_screen_vector_rgb565(unsigned char *in, unsigned char *out);
void draw_screen_vector_rgba8888(unsigned char *in, unsigned char *out);

void draw_screen_vector(unsigned char *in, unsigned char *out)
{
    if (fb.bits_per_pixel == 16) {
        draw_screen_vector_rgb565(in, out);
    } else if (fb.bits_per_pixel == 32) {
        draw_screen_vector_rgba8888(in, out);
    }
}

void draw_screen_vector_rgb565(unsigned char *in, unsigned char *out)
{
    int y;
    int x_offset, x_offset_end;

    y = SCREENHEIGHT;
    
    if (fb_scaling == 1) {
        // Direct to framebuffer - use proper centering
        int temp = ((int)fb.xres - (SCREENWIDTH * fb_scaling)) << 1;
        x_offset     = temp >> 1;
        x_offset_end = temp - x_offset;
    } else {
        // To intermediate buffer - no offsets needed (320x200 fixed size)
        x_offset = 0;
        x_offset_end = 0;
    }

    // Palette loading is done directly in inline assembly

    while (y--)
    {
        out += x_offset;

        int elements_to_process = SCREENWIDTH;

        while (elements_to_process)
        {
            size_t vl = __riscv_vsetvl_e8m8(elements_to_process);

            vuint8m8_t pixel_index = __riscv_vle8_v_u8m8(in, vl);

            // Use inline assembly for efficient vrgather and store operations
            // We'll calculate proper vl for each vsseg2e8 using vsetvli
            
            __asm__ volatile(
                // Set vector length for m8 operations (vrgather)
                "vsetvli zero, %1, e8, m8, ta, ma\n\t"
                
                // Copy pixel indices to v8 to avoid overlap issues
                "vmv8r.v v8, %0\n\t"             // Copy pixel_index to v8
                
                // Load upper palette and perform vrgather
                "vl8r.v v24, (%2)\n\t"           // Load upper palette to v24
                "vrgather.vv v16, v24, v8\n\t"   // v16 = upper[pixel_index_8]
                
                // Load lower palette and perform vrgather  
                "vl8r.v v24, (%3)\n\t"           // Load lower palette to v24
                "vrgather.vv v0, v24, v8\n\t"    // v0 = lower[pixel_index_8] (safe, v0 ≠ v8)
                
                // Calculate first batch VL for m4 operations (vsseg2e8)
                "vsetvli t0, %1, e8, m4, ta, ma\n\t"     // t0 = actual vl for first vsseg2e8
                
                // First batch: Extract upper0,lower0 using vmv4r and store
                "vmv4r.v v8, v0\n\t"             // lower0 -> v8-v11 (first batch)
                "vmv4r.v v12, v16\n\t"           // upper0 -> v12-v15 (first batch)
                "vsseg2e8.v v8, (%4)\n\t"        // Store lower,upper first batch
                
                // Calculate remaining elements and second batch VL
                "sub t1, %1, t0\n\t"             // t1 = remaining elements
                "beqz t1, 1f\n\t"                // Skip second batch if no remaining elements
                "vsetvli t1, t1, e8, m4, ta, ma\n\t"     // t1 = actual vl for second vsseg2e8
                
                // Calculate second batch output address
                "slli t2, t0, 1\n\t"             // t2 = first_batch_vl * 2 (bytes per pixel)
                "add t2, %4, t2\n\t"             // t2 = out + first_batch_vl * 2
                
                // Second batch: Extract upper1,lower1 using vmv4r and store
                "vmv4r.v v8, v4\n\t"             // lower1 -> v8-v11 (second batch, v0+4=v4)
                "vmv4r.v v12, v20\n\t"           // upper1 -> v12-v15 (second batch, v16+4=v20)
                "vsseg2e8.v v8, (t2)\n\t"        // Store lower,upper second batch
                "1:\n\t"                          // End label
                
                :: "vr"(pixel_index),                             // %0
                   "r"(vl),                                       // %1 (full vl for vrgather)
                   "r"(vpalette_upper),                           // %2 (upper palette address)
                   "r"(vpalette_lower),                           // %3 (lower palette address)
                   "r"(out)                                       // %4 (output address)
                : "t0", "t1", "t2",
                  "v8", "v9",
                  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
                  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
                  "v30", "v31", "vl", "vtype", "vxrm", "vxsat", "memory"
            );

            in += vl;
            out += sizeof(uint16_t) * vl;
            elements_to_process -= vl;
        }
        // Advance to next framebuffer line properly
        // We need to skip to the start of the next line in the framebuffer
        out += x_offset_end;
    }
}

void draw_screen_vector_rgba8888(unsigned char *in, unsigned char *out)
{
    int y;
    int x_offset, x_offset_end;

    y = SCREENHEIGHT;
    
    if (fb_scaling == 1) {
        // Direct to framebuffer - use proper centering
        int temp = ((int)fb.xres - (SCREENWIDTH * fb_scaling)) << 2;
        x_offset     = temp >> 1;
        x_offset_end = temp - x_offset;
    } else {
        // To intermediate buffer - no offsets needed (320x200 fixed size)
        x_offset = 0;
        x_offset_end = 0;
    }

    while (y--)
    {
        out += x_offset;

        int elements_to_process = SCREENWIDTH;

        while (elements_to_process)
        {
            size_t vl = __riscv_vsetvl_e8m8(elements_to_process);

            vuint8m8_t pixel_index = __riscv_vle8_v_u8m8(in, vl);

            __asm__ volatile(
                // Set vector length for m8 operations (vrgather)
                "vsetvli zero, %1, e8, m8, ta, ma\n\t"
                
                // Copy pixel indices to v8 to avoid overlap issues
                "vmv8r.v v8, %0\n\t"             // Copy pixel_index to v8
                
                // Load upper palette and perform vrgather
                "vl8r.v v24, (%2)\n\t"           // Load upper palette to v24
                "vrgather.vv v16, v24, v8\n\t"   // v16 = upper[pixel_index_8] = {B[4:0], G[5:3]}
                
                // Load lower palette and perform vrgather  
                "vl8r.v v24, (%3)\n\t"           // Load lower palette to v24
                "vrgather.vv v0, v24, v8\n\t"    // v0 = lower[pixel_index_8] = {G[2:0], R[4:0]}
                
                // Now we have:
                // v16 = upper = {B[4:0], G[5:3]} - B in bits [7:3], G[5:3] in bits [2:0]
                // v0  = lower = {G[2:0], R[4:0]} - G[2:0] in bits [7:5], R in bits [4:0]
                
                // Extract Red channel (use v8 as destination)
                "vsll.vi v8, v0, 3\n\t"          // v8 = red_8bit (R channel, lower 5 bits shifted)
                
                // Extract Green channel components (use v24 as temp)
                "vsll.vi v24, v16, 5\n\t"        // v24 = G[5:3] from upper << 5 (to bits [7:5])
                "vsrl.vi v0, v0, 3\n\t"          // v0 = G[2:0] from lower >> 3 (to bits [4:2])
                "vor.vv v0, v24, v0\n\t"         // v0 = G[5:0] combined
                
                // Extract Blue channel (overwrite v16)
                "vand.vi v16, v16, -8\n\t"       // v16 = clear lower 3 bits (keep B[4:0] in upper 5 bits)
                
                // Now we have: v8=R, v0=G, v16=B
                
                // Initialize tracking registers
                "mv t1, %1\n\t"                  // t1 = remaining elements
                "mv t2, %4\n\t"                  // t2 = current output pointer
                
                // Batch 1
                "vsetvli t0, t1, e8, m2, ta, ma\n\t"     // t0 = actual vl for first vsseg4e8
                "vmv2r.v v24, v16\n\t"           // B0 -> v24-v25 (batch 1) - swapped with R
                "vmv2r.v v26, v0\n\t"            // G0 -> v26-v27 (batch 1)
                "vmv2r.v v28, v8\n\t"            // R0 -> v28-v29 (batch 1) - swapped with B
                "vmv.v.i v30, 0\n\t"             // v30 = alpha = 0 (needs LMUL=2)
                "vsseg4e8.v v24, (t2)\n\t"       // Store BGRA batch 1
                "sub t1, t1, t0\n\t"             // t1 = remaining elements after batch 1
                "slli t3, t0, 2\n\t"             // t3 = batch1_vl * 4 (bytes per pixel)
                "add t2, t2, t3\n\t"             // t2 = update output pointer
                "beqz t1, 4f\n\t"                // Skip remaining batches if no remaining elements
                
                // Batch 2
                "vsetvli t0, t1, e8, m2, ta, ma\n\t"     // t0 = actual vl for second vsseg4e8
                "vmv2r.v v24, v18\n\t"           // B1 -> v24-v25 (batch 2, v16+2=v18) - swapped with R
                "vmv2r.v v26, v2\n\t"            // G1 -> v26-v27 (batch 2, v0+2=v2)
                "vmv2r.v v28, v10\n\t"           // R1 -> v28-v29 (batch 2, v8+2=v10) - swapped with B
                "vsseg4e8.v v24, (t2)\n\t"       // Store BGRA batch 2
                "sub t1, t1, t0\n\t"             // t1 = remaining elements after batch 2
                "slli t3, t0, 2\n\t"             // t3 = batch2_vl * 4 (bytes per pixel)
                "add t2, t2, t3\n\t"             // t2 = update output pointer
                "beqz t1, 4f\n\t"                // Skip remaining batches if no remaining elements
                
                // Batch 3
                "vsetvli t0, t1, e8, m2, ta, ma\n\t"     // t0 = actual vl for third vsseg4e8
                "vmv2r.v v24, v20\n\t"           // B2 -> v24-v25 (batch 3, v16+4=v20) - swapped with R
                "vmv2r.v v26, v4\n\t"            // G2 -> v26-v27 (batch 3, v0+4=v4)
                "vmv2r.v v28, v12\n\t"           // R2 -> v28-v29 (batch 3, v8+4=v12) - swapped with B
                "vsseg4e8.v v24, (t2)\n\t"       // Store BGRA batch 3
                "sub t1, t1, t0\n\t"             // t1 = remaining elements after batch 3
                "slli t3, t0, 2\n\t"             // t3 = batch3_vl * 4 (bytes per pixel)
                "add t2, t2, t3\n\t"             // t2 = update output pointer
                "beqz t1, 4f\n\t"                // Skip last batch if no remaining elements
                
                // Batch 4
                "vsetvli t0, t1, e8, m2, ta, ma\n\t"     // t0 = actual vl for fourth vsseg4e8
                "vmv2r.v v24, v22\n\t"           // B3 -> v24-v25 (batch 4, v16+6=v22) - swapped with R
                "vmv2r.v v26, v6\n\t"            // G3 -> v26-v27 (batch 4, v0+6=v6)
                "vmv2r.v v28, v14\n\t"           // R3 -> v28-v29 (batch 4, v8+6=v14) - swapped with B
                "vsseg4e8.v v24, (t2)\n\t"       // Store BGRA batch 4
                
                "4:\n\t"                          // End label
                
                :: "vr"(pixel_index),                             // %0
                   "r"(vl),                                       // %1 (full vl for vrgather)
                   "r"(vpalette_upper),                           // %2 (upper palette address)
                   "r"(vpalette_lower),                           // %3 (lower palette address)
                   "r"(out)                                       // %4 (output address)
                : "t0", "t1", "t2", "t3", "t4", "t5", "t6",
                  "v8", "v9",
                  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
                  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
                  "v30", "v31", "vl", "vtype", "vxrm", "vxsat", "memory"
            );

            in += vl;
            out += sizeof(uint32_t) * vl;
            elements_to_process -= vl;
        }
        out += x_offset_end;
    }
}

void scale_intermediate_to_framebuffer_rgb565(unsigned char *intermediate, unsigned char *framebuffer, int scale_factor)
{
    int y;
    int fb_width = fb.xres;
    int fb_height = fb.yres;
    
    // Calculate offsets - center X, align to top for Y (like original fbdoom)
    int x_offset = (fb_width - (SCREENWIDTH * scale_factor)) / 2;
    int y_offset = 0;
    
    for (y = 0; y < SCREENHEIGHT; y++) {
        unsigned char *src_line = intermediate + y * SCREENWIDTH * 2; // 2 bytes per pixel for RGB565
        
        for (int scale_y = 0; scale_y < scale_factor; scale_y++) {
            int dst_y = y_offset + y * scale_factor + scale_y;
            if (dst_y >= fb_height) break;
            
            unsigned char *dst_line = framebuffer + dst_y * fb_width * 2 + x_offset * 2;
            
            int elements_to_process = SCREENWIDTH;
            unsigned char *src_ptr = src_line;
            unsigned char *dst_ptr = dst_line;
            
            while (elements_to_process > 0) {
                size_t vl = __riscv_vsetvl_e16m1(elements_to_process);
                
                // Load pixels from intermediate buffer
                vuint16m1_t pixels = __riscv_vle16_v_u16m1((uint16_t*)src_ptr, vl);
                
                // Use vsseg[2-8]e16 to store duplicated pixels
                switch (scale_factor) {
                    case 2:
                        __riscv_vsseg2e16_v_u16m1x2((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x2(pixels, pixels), vl);
                        break;
                    case 3:
                        __riscv_vsseg3e16_v_u16m1x3((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x3(pixels, pixels, pixels), vl);
                        break;
                    case 4:
                        __riscv_vsseg4e16_v_u16m1x4((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x4(pixels, pixels, pixels, pixels), vl);
                        break;
                    case 5:
                        __riscv_vsseg5e16_v_u16m1x5((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x5(pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 6:
                        __riscv_vsseg6e16_v_u16m1x6((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x6(pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 7:
                        __riscv_vsseg7e16_v_u16m1x7((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x7(pixels, pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 8:
                        __riscv_vsseg8e16_v_u16m1x8((uint16_t*)dst_ptr, __riscv_vcreate_v_u16m1x8(pixels, pixels, pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                }
                
                src_ptr += vl * 2;
                dst_ptr += vl * scale_factor * 2;
                elements_to_process -= vl;
            }
        }
    }
}

void scale_intermediate_to_framebuffer_rgba8888(unsigned char *intermediate, unsigned char *framebuffer, int scale_factor)
{
    int y;
    int fb_width = fb.xres;
    int fb_height = fb.yres;
    
    // Calculate offsets - center X, align to top for Y (like original fbdoom)
    int x_offset = (fb_width - (SCREENWIDTH * scale_factor)) / 2;
    int y_offset = 0;
    
    for (y = 0; y < SCREENHEIGHT; y++) {
        unsigned char *src_line = intermediate + y * SCREENWIDTH * 4; // 4 bytes per pixel for RGBA8888
        
        for (int scale_y = 0; scale_y < scale_factor; scale_y++) {
            int dst_y = y_offset + y * scale_factor + scale_y;
            if (dst_y >= fb_height) break;
            
            unsigned char *dst_line = framebuffer + dst_y * fb_width * 4 + x_offset * 4;
            
            int elements_to_process = SCREENWIDTH;
            unsigned char *src_ptr = src_line;
            unsigned char *dst_ptr = dst_line;
            
            while (elements_to_process > 0) {
                size_t vl = __riscv_vsetvl_e32m1(elements_to_process);
                
                // Load pixels from intermediate buffer
                vuint32m1_t pixels = __riscv_vle32_v_u32m1((uint32_t*)src_ptr, vl);
                
                // Use vsseg[2-8]e32 to store duplicated pixels
                switch (scale_factor) {
                    case 2:
                        __riscv_vsseg2e32_v_u32m1x2((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x2(pixels, pixels), vl);
                        break;
                    case 3:
                        __riscv_vsseg3e32_v_u32m1x3((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x3(pixels, pixels, pixels), vl);
                        break;
                    case 4:
                        __riscv_vsseg4e32_v_u32m1x4((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x4(pixels, pixels, pixels, pixels), vl);
                        break;
                    case 5:
                        __riscv_vsseg5e32_v_u32m1x5((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x5(pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 6:
                        __riscv_vsseg6e32_v_u32m1x6((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x6(pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 7:
                        __riscv_vsseg7e32_v_u32m1x7((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x7(pixels, pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                    case 8:
                        __riscv_vsseg8e32_v_u32m1x8((uint32_t*)dst_ptr, __riscv_vcreate_v_u32m1x8(pixels, pixels, pixels, pixels, pixels, pixels, pixels, pixels), vl);
                        break;
                }
                
                src_ptr += vl * 4;
                dst_ptr += vl * scale_factor * 4;
                elements_to_process -= vl;
            }
        }
    }
}

void scale_intermediate_to_framebuffer(unsigned char *intermediate, unsigned char *framebuffer, int scale_factor)
{
    if (fb.bits_per_pixel == 16) {
        scale_intermediate_to_framebuffer_rgb565(intermediate, framebuffer, scale_factor);
    } else if (fb.bits_per_pixel == 32) {
        scale_intermediate_to_framebuffer_rgba8888(intermediate, framebuffer, scale_factor);
    }
}
#endif

void I_InitGraphics (void)
{
    /* Open fbdev file descriptor */
    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
    {
        printf("Could not open /dev/fb0");
        exit(-1);
    }

    /* fetch framebuffer info */
    ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb);
    /* change params if needed */
    //ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb);
    printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, x_virtual: %d, y_virtual: %d, bpp: %d, grayscale: %d\n",
            fb.xres, fb.yres, fb.xres_virtual, fb.yres_virtual, fb.bits_per_pixel, fb.grayscale);

    printf("I_InitGraphics: framebuffer: RGBA: %d%d%d%d, red_off: %d, green_off: %d, blue_off: %d, transp_off: %d\n",
            fb.red.length, fb.green.length, fb.blue.length, fb.transp.length, fb.red.offset, fb.green.offset, fb.blue.offset, fb.transp.offset);

    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);

    int i = M_CheckParmWithArgs("-scaling", 1);
    if (i > 0) {
        i = atoi(myargv[i + 1]);
#ifdef USE_VECTOR
        // Limit scaling to 8x for vector implementation
        if (i > 8) i = 8;
#endif
        fb_scaling = i;
        printf("I_InitGraphics: Scaling factor: %d\n", fb_scaling);
    } else {
        fb_scaling = fb.xres / SCREENWIDTH;
        if (fb.yres / SCREENHEIGHT < fb_scaling)
            fb_scaling = fb.yres / SCREENHEIGHT;
#ifdef USE_VECTOR
        // Limit auto-scaling to 8x for vector implementation
        if (fb_scaling > 8) fb_scaling = 8;
#endif
        printf("I_InitGraphics: Auto-scaling factor: %d\n", fb_scaling);
    }
    /* Allocate screen to draw to */
	I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // For DOOM to draw on
	// I_VideoBuffer_FB = (byte*)malloc(fb.xres * fb.yres * (fb.bits_per_pixel/8));     // For a single write() syscall to fbdev

#ifdef USE_VECTOR
    // Allocate intermediate buffer for vector scaling (fixed 320x200 size in framebuffer format)
    I_VideoBuffer_Intermediate = (byte*)malloc(SCREENWIDTH * SCREENHEIGHT * (fb.bits_per_pixel/8));
#endif

    // SMD: use mmap to avoid extra copies between internal buffer and framebuffer
    I_VideoBuffer_FB = mmap(NULL, fb.xres * fb.yres * (fb.bits_per_pixel/8), PROT_WRITE, MAP_SHARED, fd_fb, 0);

	screenvisible = true;

    extern int I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void)
{
	Z_Free (I_VideoBuffer);
#ifdef USE_VECTOR
	if (I_VideoBuffer_Intermediate) {
		free(I_VideoBuffer_Intermediate);
	}
#endif
	free(I_VideoBuffer_FB);
}

void I_StartFrame (void)
{

}

__attribute__ ((weak)) void I_GetEvent (void)
{
//	event_t event;
//	bool button_state;
//
//	button_state = button_read ();
//
//	if (last_button_state != button_state)
//	{
//		last_button_state = button_state;
//
//		event.type = last_button_state ? ev_keydown : ev_keyup;
//		event.data1 = KEY_FIRE;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		D_PostEvent (&event);
//	}
//
//	touch_main ();
//
//	if ((touch_state.x != last_touch_state.x) || (touch_state.y != last_touch_state.y) || (touch_state.status != last_touch_state.status))
//	{
//		last_touch_state = touch_state;
//
//		event.type = (touch_state.status == TOUCH_PRESSED) ? ev_keydown : ev_keyup;
//		event.data1 = -1;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		if ((touch_state.x > 49)
//		 && (touch_state.x < 72)
//		 && (touch_state.y > 104)
//		 && (touch_state.y < 143))
//		{
//			// select weapon
//			if (touch_state.x < 60)
//			{
//				// lower row (5-7)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '5';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '6';
//				}
//				else
//				{
//					event.data1 = '1';
//				}
//			}
//			else
//			{
//				// upper row (2-4)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '2';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '3';
//				}
//				else
//				{
//					event.data1 = '4';
//				}
//			}
//		}
//		else if (touch_state.x < 40)
//		{
//			// button bar at bottom screen
//			if (touch_state.y < 40)
//			{
//				// enter
//				event.data1 = KEY_ENTER;
//			}
//			else if (touch_state.y < 80)
//			{
//				// escape
//				event.data1 = KEY_ESCAPE;
//			}
//			else if (touch_state.y < 120)
//			{
//				// use
//				event.data1 = KEY_USE;
//			}
//			else if (touch_state.y < 160)
//			{
//				// map
//				event.data1 = KEY_TAB;
//			}
//			else if (touch_state.y < 200)
//			{
//				// pause
//				event.data1 = KEY_PAUSE;
//			}
//			else if (touch_state.y < 240)
//			{
//				// toggle run
//				if (touch_state.status == TOUCH_PRESSED)
//				{
//					run = !run;
//
//					event.data1 = KEY_RSHIFT;
//
//					if (run)
//					{
//						event.type = ev_keydown;
//					}
//					else
//					{
//						event.type = ev_keyup;
//					}
//				}
//				else
//				{
//					return;
//				}
//			}
//			else if (touch_state.y < 280)
//			{
//				// save
//				event.data1 = KEY_F2;
//			}
//			else if (touch_state.y < 320)
//			{
//				// load
//				event.data1 = KEY_F3;
//			}
//		}
//		else
//		{
//			// movement/menu navigation
//			if (touch_state.x < 100)
//			{
//				if (touch_state.y < 100)
//				{
//					event.data1 = KEY_STRAFE_L;
//				}
//				else if (touch_state.y < 220)
//				{
//					event.data1 = KEY_DOWNARROW;
//				}
//				else
//				{
//					event.data1 = KEY_STRAFE_R;
//				}
//			}
//			else if (touch_state.x < 180)
//			{
//				if (touch_state.y < 160)
//				{
//					event.data1 = KEY_LEFTARROW;
//				}
//				else
//				{
//					event.data1 = KEY_RIGHTARROW;
//				}
//			}
//			else
//			{
//				event.data1 = KEY_UPARROW;
//			}
//		}
//
//		D_PostEvent (&event);
//	}
}

__attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void)
{
}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{
    static int	lasttic;
    int		tics;
    int		i;
    // draws little dots on the bottom of the screen

    if (display_fps_dots)
    {
	i = I_GetTime();
	tics = i - lasttic;
	lasttic = i;
	if (tics > 20) tics = 20;

	for (i=0 ; i<tics*4 ; i+=4)
	    I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
	for ( ; i<20*4 ; i+=4)
	    I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
    }

#ifdef USE_VECTOR
    if (fb_scaling == 1) {
        // Direct rendering to framebuffer for 1x scaling
        draw_screen_vector((unsigned char *) I_VideoBuffer, (unsigned char *) I_VideoBuffer_FB);
    } else {
        // Two-stage rendering for scaling > 1x
        // First render to intermediate buffer (320x200 fixed size)
        draw_screen_vector((unsigned char *) I_VideoBuffer, (unsigned char *) I_VideoBuffer_Intermediate);
        // Then scale from intermediate buffer to framebuffer
        scale_intermediate_to_framebuffer((unsigned char *) I_VideoBuffer_Intermediate, (unsigned char *) I_VideoBuffer_FB, fb_scaling);
    }
#else
    int y;
    int x_offset, x_offset_end;
    unsigned char *line_in, *line_out;

    /* Offsets in case FB is bigger than DOOM */
    /* 600 = fb heigt, 200 screenheight */
    /* 600 = fb heigt, 200 screenheight */
    /* 2048 =fb width, 320 screenwidth */
    x_offset     = (((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8)) / 2; // XXX: siglent FB hack: /4 instead of /2, since it seems to handle the resolution in a funny way
    //x_offset     = 0;
    x_offset_end = ((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8) - x_offset;

    /* DRAW SCREEN */
    line_in  = (unsigned char *) I_VideoBuffer;
    line_out = (unsigned char *) I_VideoBuffer_FB;

    y = SCREENHEIGHT;

    while (y--)
    {
        int i;
        for (i = 0; i < fb_scaling; i++) {
            line_out += x_offset;
#ifdef CMAP256
            for (fb_scaling == 1) {
                memcpy(line_out, line_in, SCREENWIDTH); /* fb_width is bigger than Doom SCREENWIDTH... */
            } else {
                //XXX FIXME fb_scaling support!
            }
#else
            //cmap_to_rgb565((void*)line_out, (void*)line_in, SCREENWIDTH);
            cmap_to_fb((void*)line_out, (void*)line_in, SCREENWIDTH);
#endif
            line_out += (SCREENWIDTH * fb_scaling * (fb.bits_per_pixel/8)) + x_offset_end;
        }
        line_in += SCREENWIDTH;
    }
#endif
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
    //col_t* c;

    //for (i = 0; i < 256; i++)
    //{
    //  c = (col_t*)palette;

    //  rgb565_palette[i] = GFX_RGB565(gammatable[usegamma][c->r],
    //                                 gammatable[usegamma][c->g],
    //                                 gammatable[usegamma][c->b]);

    //  palette += 3;
    //}

#ifdef USE_VECTOR
    /* Build palette for vector drawing */

    // Process 256 colors in four batches of 64 for VLEN=256 (LMUL=2)
    for (int batch = 0; batch < 4; batch++) {
        byte* palette_offset = palette + batch * 64 * 3;
        uint8_t* upper_offset = vpalette_upper + batch * 64;
        uint8_t* lower_offset = vpalette_lower + batch * 64;
        
        // Load RGB values using segmented load (more efficient than strided)
        vuint8m2x3_t rgb_tuple = __riscv_vlseg3e8_v_u8m2x3(palette_offset, 64);
        vuint8m2_t vri_8m2 = __riscv_vget_v_u8m2x3_u8m2(rgb_tuple, 0);
        vuint8m2_t vgi_8m2 = __riscv_vget_v_u8m2x3_u8m2(rgb_tuple, 1);
        vuint8m2_t vbi_8m2 = __riscv_vget_v_u8m2x3_u8m2(rgb_tuple, 2);

        // Apply gamma correction using indexed load
        vuint8m2_t vr_8m2 = __riscv_vluxei8_v_u8m2(gammatable[usegamma], vri_8m2, 64);
        vuint8m2_t vg_8m2 = __riscv_vluxei8_v_u8m2(gammatable[usegamma], vgi_8m2, 64);
        vuint8m2_t vb_8m2 = __riscv_vluxei8_v_u8m2(gammatable[usegamma], vbi_8m2, 64);

        // Downscale pixel resolution based on framebuffer pixel format
        vr_8m2 = __riscv_vsrl_vx_u8m2(vr_8m2, 8 - 5, 64);
        vg_8m2 = __riscv_vsrl_vx_u8m2(vg_8m2, 8 - 6, 64);
        vb_8m2 = __riscv_vsrl_vx_u8m2(vb_8m2, 8 - 5, 64);

        // Construct the pixel (16bits) with vr|vg|vb
        vuint16m4_t vr_16m4 = __riscv_vzext_vf2_u16m4(vr_8m2, 64);
        vuint16m4_t vg_16m4 = __riscv_vzext_vf2_u16m4(vg_8m2, 64);
        vuint16m4_t vb_16m4 = __riscv_vzext_vf2_u16m4(vb_8m2, 64);

        vr_16m4 = __riscv_vsll_vx_u16m4(vr_16m4, 0, 64);
        vg_16m4 = __riscv_vsll_vx_u16m4(vg_16m4, 5, 64);
        vb_16m4 = __riscv_vsll_vx_u16m4(vb_16m4, 11, 64);

        vuint16m4_t pixel = __riscv_vor_vv_u16m4(vr_16m4, vg_16m4, 64);
        pixel = __riscv_vor_vv_u16m4(pixel, vb_16m4, 64);

        // Split 16-bit pixels into upper and lower 8-bit channels
        vuint8m2_t pixel_upper = __riscv_vnsrl_wx_u8m2(pixel, 8, 64);
        vuint8m2_t pixel_lower = __riscv_vnsrl_wx_u8m2(pixel, 0, 64);

        // Store the precomputed pixels in separate arrays
        __riscv_vse8_v_u8m2(upper_offset, pixel_upper, 64);
        __riscv_vse8_v_u8m2(lower_offset, pixel_lower, 64);
    }

#else

    int i;
    /* performance boost:
     * map to the right pixel format over here! */

    for (i=0; i<256; ++i ) {
        colors[i].a = 0;
        colors[i].r = gammatable[usegamma][*palette++];
        colors[i].g = gammatable[usegamma][*palette++];
        colors[i].b = gammatable[usegamma][*palette++];
    }
#endif

    /* Set new color map in kernel framebuffer driver */
    //XXX FIXME ioctl(fd_fb, IOCTL_FB_PUTCMAP, colors);
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
#ifndef USE_VECTOR
    int i, best, best_diff, diff;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;
    for (i = 0; i < 256; ++i)
    {
    	color.r = GFX_RGB565_R(rgb565_palette[i]);
    	color.g = GFX_RGB565_G(rgb565_palette[i]);
    	color.b = GFX_RGB565_B(rgb565_palette[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }
    return best;
#else
    return 0;
#endif
}

void I_BeginRead (void)
{
}

void I_EndRead (void)
{
}

void I_SetWindowTitle (char *title)
{
}

void I_GraphicsCheckCommandLine (void)
{
}

void I_SetGrabMouseCallback (grabmouse_callback_t func)
{
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables (void)
{
}

void I_DisplayFPSDots (boolean dots_on)
{
    display_fps_dots = dots_on;
}

void I_CheckIsScreensaver (void)
{
}
