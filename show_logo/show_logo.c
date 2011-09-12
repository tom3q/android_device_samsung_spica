/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <cutils/log.h>

#define LOG_TAG "bootlogo"

#include <png.h>

struct FB {
    unsigned short *bits;
    unsigned size;
    int fd;
    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;
};

#define fb_width(fb) ((fb)->vi.xres)
#define fb_height(fb) ((fb)->vi.yres)
#define fb_size(fb) ((fb)->vi.yres * (fb)->fi.line_length)

static int fb_open(struct FB *fb)
{
    fb->fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb->fd < 0)
        return -1;

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        goto fail;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vi) < 0)
        goto fail;

    fb->bits = mmap(0, fb_size(fb), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fb->fd, 0);
    if (fb->bits == MAP_FAILED)
        goto fail;

    return 0;

fail:
    close(fb->fd);
    return -1;
}

static void fb_close(struct FB *fb)
{
    munmap(fb->bits, fb_size(fb));
    close(fb->fd);
}

static void fb_update(struct FB *fb)
{
    fb->vi.yoffset = 0;
    ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->vi);
}

static int vt_set_mode(int graphics)
{
    int fd, r;
    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (fd < 0)
        return -1;
    r = ioctl(fd, KDSETMODE, (void*) (graphics ? KD_GRAPHICS : KD_TEXT));
    close(fd);
    return r;
}

static void draw_row16(struct FB *fb, void *dst, png_byte *row)
{
    unsigned int x;
    unsigned char r, g, b;
    unsigned short *pix16 = (unsigned short *)dst;
    unsigned int pix;

    for (x = 0; x < fb->vi.xres; ++x) {
        r = *row++;
        g = *row++;
        b = *row++;
        r >>= 8 - fb->vi.red.length;
        g >>= 8 - fb->vi.green.length;
        b >>= 8 - fb->vi.blue.length;
        pix = r << fb->vi.red.offset
            | g << fb->vi.green.offset
            | b << fb->vi.blue.offset;
        *pix16++ = pix & 0xffff;
    }
}

static void draw_row32(struct FB *fb, void *dst, png_byte *row)
{
    unsigned int x;
    unsigned char r, g, b;
    unsigned int *pix32 = (unsigned int *)dst;
    unsigned int pix;

    for (x = 0; x < fb->vi.xres; ++x) {
        r = *row++;
        g = *row++;
        b = *row++;
        r >>= 8 - fb->vi.red.length;
        g >>= 8 - fb->vi.green.length;
        b >>= 8 - fb->vi.blue.length;
        pix = r << fb->vi.red.offset
            | g << fb->vi.green.offset
            | b << fb->vi.blue.offset;
        *pix32++ = pix;
    }
}

struct png_mem_ptr {
    unsigned char *data;
    unsigned char *ptr;
    size_t size;
};

static void png_mapped_read(png_structp png_ptr,
                                            png_bytep data, png_size_t length)
{
    struct png_mem_ptr *mem = png_ptr->io_ptr;

    if (mem->ptr + length > mem->data + mem->size)
        png_error(png_ptr, "Tried to read beyond the file");

    memcpy(data, mem->ptr, length);
    mem->ptr += length;
}

static int load_png_image(char *fn)
{
    unsigned char *data, *line;
    struct png_mem_ptr png_mem;
    unsigned int width, height;
    png_bytep *row_pointers;
    png_byte color_type;
    png_byte bit_depth;
    unsigned int y;
    struct stat s;
    struct FB fb;
    int ret = -1;
    int fd;

    if (vt_set_mode(1))
        return -1;

    fd = open(fn, O_RDONLY);
    if (fd < 0)
        goto fail_restore_text;

    if (fstat(fd, &s) < 0) {
        LOGE("Failed to stat boot logo file %s", fn);
        goto fail_close_file;
    }

    if (fb_open(&fb)) {
        LOGE("Failed to open framebuffer device");
        goto fail_close_file;
    }

    line = fb.bits;

    data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        LOGE("Failed to mmap boot logo file %s", fn);
        goto fail_close_fb;
    }

    if (png_sig_cmp((png_bytep)data, 0, 8)) {
        LOGE("File %s is not a valid PNG image", fn);
        goto fail_unmap_data;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                            NULL, NULL, NULL);
    if (!png_ptr) {
        LOGE("Failed to allocate png_ptr");
        goto fail_unmap_data;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        LOGE("Failed to allocate info_ptr");
        png_destroy_read_struct(&png_ptr,
                                (png_infopp)NULL, (png_infopp)NULL);
        goto fail_unmap_data;
    }

    png_infop end_info = png_create_info_struct(png_ptr);
    if (!end_info)
    {
        LOGE("Failed to allocate end_info");
        png_destroy_read_struct(&png_ptr, &info_ptr,
                                (png_infopp)NULL);
        goto fail_unmap_data;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        LOGE("Failed to parse PNG file %s", fn);
        goto fail_free_png;
    }

    png_mem.data = (unsigned char *)data;
    png_mem.ptr = (unsigned char *)data;
    png_mem.size = s.st_size;
    png_set_read_fn(png_ptr, &png_mem, png_mapped_read);

    png_read_info(png_ptr, info_ptr);

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (width != fb.vi.xres || height != fb.vi.yres) {
        LOGE("File %s has invalid resolution (%dx%d)", fn, width, height);
        goto fail_unmap_data;
    }

    if (color_type != PNG_COLOR_TYPE_RGB) {
        LOGE("File %s has invalid color type (%d)", fn, color_type);
        goto fail_unmap_data;
    }

    if (bit_depth != 8) {
        LOGE("File %s has invalid bit depth (%d)", fn, bit_depth);
        goto fail_unmap_data;
    }

    row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
    for (y=0; y<height; y++)
        row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr,info_ptr));

    /* read file */
    if (setjmp(png_jmpbuf(png_ptr))) {
        LOGE("Failed to read image data");
        goto fail_free_rows;
    }

    png_read_image(png_ptr, row_pointers);

    if (fb.vi.bits_per_pixel == 16) {
        for (y=0; y<height; y++) {
            draw_row16(&fb, line, row_pointers[y]);
            line += fb.fi.line_length;
        }
    } else {
        for (y=0; y<height; y++) {
            draw_row32(&fb, line, row_pointers[y]);
            line += fb.fi.line_length;
        }
    }

    fb_update(&fb);

    ret = 0;

    /* Fall through */
fail_free_rows:
    for (y=0; y<height; y++)
        free(row_pointers[y]);
    free(row_pointers);
fail_free_png:
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
fail_unmap_data:
    munmap(data, s.st_size);
fail_close_fb:
    fb_close(&fb);
fail_close_file:
    close(fd);
fail_restore_text:
    if (ret)
        vt_set_mode(0);

    return ret;
}

#define USER_BOOT_LOGO_PATH     "/data/bootlogo.png"
#define SYSTEM_BOOT_LOGO_PATH   "/system/etc/bootlogo.png"
#define DEFAULT_BOOT_LOGO_PATH  "/bootlogo.png"

int main(int argc, char **argv)
{
    int ret;

    LOGI("Trying to read boot logo from %s", USER_BOOT_LOGO_PATH);
    ret = load_png_image(USER_BOOT_LOGO_PATH);
    if (!ret) {
        LOGI("Loaded boot logo from %s.", USER_BOOT_LOGO_PATH);
        return 0;
    }

    LOGI("Trying to read boot logo from %s", SYSTEM_BOOT_LOGO_PATH);
    ret = load_png_image(SYSTEM_BOOT_LOGO_PATH);
    if (!ret) {
        LOGI("Loaded boot logo from %s.", SYSTEM_BOOT_LOGO_PATH);
        return 0;
    }

    LOGI("Trying to read boot logo from %s", DEFAULT_BOOT_LOGO_PATH);
    ret = load_png_image(DEFAULT_BOOT_LOGO_PATH);
    if (!ret) {
        LOGI("Loaded boot logo from %s.", DEFAULT_BOOT_LOGO_PATH);
    } else {
        LOGE("Failed to load boot logo.");
    }

    return ret;
}
