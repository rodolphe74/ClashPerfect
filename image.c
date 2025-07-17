#include "global.h"
#include "image.h"
#include "dither.h"
#include <math.h>
#include <stdio.h>

static inline unsigned long get_color_hash_index(uint8_t r, uint8_t g, uint8_t b)
{
	return ((unsigned long)r << 16) | ((unsigned long)g << 8) | (unsigned long)b;
}

long count_unique_colors_hashed(const unsigned char *image_data, int width, int height)
{
	if (image_data == NULL || width <= 0 || height <= 0) {
		printf("Erreur: Données d'image invalides ou dimensions non valides.\n");
		return 0;
	}

	const int components = 3; // On suppose 3 composants (RVB)
	long total_pixels = (long)width * height;

	const unsigned long MAX_24BIT_COLORS = 1UL << 24; // 2^24 = 16,777,216

	bool *color_seen = (bool *)calloc(MAX_24BIT_COLORS, sizeof(bool));
	if (color_seen == NULL) {
		printf("Erreur: Impossible d'allouer de la mémoire pour la table de hachage des couleurs (%llu octets).\n",
			   MAX_24BIT_COLORS * sizeof(bool));
		return 0;
	}

	long unique_colors_count = 0;

	for (long i = 0; i < total_pixels; ++i) {
		uint8_t r = image_data[i * components + 0];
		uint8_t g = image_data[i * components + 1];
		uint8_t b = image_data[i * components + 2];

		unsigned long hash_index = get_color_hash_index(r, g, b);

		if (!color_seen[hash_index]) {
			color_seen[hash_index] = true;
			unique_colors_count++;
		}
	}
	free(color_seen);

	return unique_colors_count;
}

uint8_t *resize_if_necessary(const uint8_t *inputImage, const int ix, const int iy, uint8_t *resizedImage, int *ox,
							 int *oy)
{
	float ratioX = 0, ratioY = 0, ratio;
	int doResize = 0;

	ratioX = ix / 320.0;
	printf("ratio x -> %f\n", ratioX);
	doResize = 1;

	if (iy > 200) {
		ratioY = iy / 200.0;
		printf("ratio y -> %f\n", ratioY);
		doResize = 1;
	}

	if (doResize) {
		ratio = fmax(ratioX, ratioY);
		printf("ratio -> %f\n", ratio);

		int xx, yy;
		xx = ix / ratio;
		yy = iy / ratio;

		printf("Nouvelles dimensions %d*%d\n", xx, yy);

		resizedImage = malloc(xx * yy * COLOR_COMP);
		stbir_resize_uint8_linear(inputImage, ix, iy, COLOR_COMP * ix, resizedImage, xx, yy, xx * COLOR_COMP,
								  COLOR_COMP);
		*ox = xx;
		*oy = yy;
		return resizedImage;
	}
	return NULL;
}

uint8_t *frame_into_canvas(const uint8_t *inputData, int ix, int iy, uint8_t *outputData, int *ox, int *oy)
{
	int targetw = 320;
	int targeth = 200;

	outputData = malloc(targetw * targeth * COLOR_COMP);
	if (outputData) {
		memset(outputData, 0, targetw * targeth * COLOR_COMP);
		int k = 0, l = 0;
		for (int j = 0; j < iy; j++) {
			for (int i = 0; i < ix; i++) {
				if (j < targeth && i < targetw) {
					outputData[(k * targetw + l) * COLOR_COMP] = inputData[(j * ix + i) * COLOR_COMP];
					outputData[(k * targetw + l) * COLOR_COMP + 1] = inputData[(j * ix + i) * COLOR_COMP + 1];
					outputData[(k * targetw + l) * COLOR_COMP + 2] = inputData[(j * ix + i) * COLOR_COMP + 2];
				}
				l++;
			}
			l = 0;
			k++;
		}
		*ox = targetw;
		*oy = targeth;
		return outputData;
	}
	return NULL;
}

unsigned char *convert_rgb_to_rgba(const unsigned char *src_image_data, int width, int height)
{
	if (src_image_data == NULL || width <= 0 || height <= 0) {
		fprintf(stderr, "Erreur: Données d'image source invalides ou dimensions non valides.\n");
		return NULL;
	}

	const int src_components = 3;  // RVB
	const int dest_components = 4; // RGBA
	long total_pixels = (long)width * height;

	size_t dest_data_size = total_pixels * dest_components * sizeof(unsigned char);

	unsigned char *dest_image_data = (unsigned char *)malloc(dest_data_size);
	if (dest_image_data == NULL) {
		fprintf(stderr, "Erreur: Impossible d'allouer de la mémoire pour l'image RGBA (%zu octets).\n", dest_data_size);
		return NULL;
	}

	for (long i = 0; i < total_pixels; ++i) {
		long src_pixel_index = i * src_components;
		long dest_pixel_index = i * dest_components;

		// Vérification de débordement pour éviter l'accès hors limites
		if ((dest_pixel_index + 3) < (long)dest_data_size && (src_pixel_index + 2) < (long)(total_pixels * src_components)) {
			dest_image_data[dest_pixel_index + 0] = src_image_data[src_pixel_index + 0]; // R
			dest_image_data[dest_pixel_index + 1] = src_image_data[src_pixel_index + 1]; // G
			dest_image_data[dest_pixel_index + 2] = src_image_data[src_pixel_index + 2]; // B
			dest_image_data[dest_pixel_index + 3] = 255; // Alpha (opaque)
		}
	}

	return dest_image_data;
}