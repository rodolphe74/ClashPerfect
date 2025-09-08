#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

static inline unsigned long get_color_hash_index(uint8_t r, uint8_t g, uint8_t b);
long count_unique_colors_hashed(const unsigned char *image_data, int width, int height);
uint8_t *resize_if_necessary(const uint8_t *inputImage, const int ix, const int iy, uint8_t *resizedImage, int *ox,
							 int *oy);
uint8_t *frame_into_canvas(const uint8_t *inputData, int ix, int iy, uint8_t *outputData, int *ox, int *oy);
unsigned char *convert_rgb_to_rgba(const unsigned char *src_image_data, int width, int height);
unsigned char *convert_rgba_to_rgb(const unsigned char* rgba, int width, int height);
#endif
