#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
// #define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
// #define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb_image_resize2.h>
#include "global.h"
#include "thomson.h"
#include "image.h"
#include "dither.h"
#include "matrix.h"
#include "k7.h"

int main(int argc, char *argv[])
{
	Color thomson_palette[4096];
	init_thomson_palette(thomson_palette);
	Color palette[PALETTE_SIZE];

	int width, height, channels;
	unsigned char *original_image = stbi_load(argv[1], &width, &height, &channels, COLOR_COMP);
	if (!original_image) {
		printf("Erreur: Impossible de charger l'image d'entrée '%s'. Vérifiez le chemin ou le format.\n", argv[1]);
		return EXIT_FAILURE;
	}

	printf("Image chargée: %s (%dx%d pixels, %d canaux d'origine)\n", argv[1], width, height, channels);

	uint8_t *resized_image = NULL;
	int wr, hr;
	resized_image = resize_if_necessary(original_image, width, height, resized_image, &wr, &hr);

	uint8_t *framed_image = NULL;
	int wf, hf;
	framed_image = frame_into_canvas(resized_image, wr, hr, framed_image, &wf, &hf);

	if (!stbi_write_png("resized.png", WIDTH, HEIGHT, COLOR_COMP, framed_image, WIDTH * 3)) {
		printf("Erreur: Impossible d'écrire l'image PNG 'resized.png'\n");
	} else {
		printf("Image sauvée avec succès au format PNG: 'resized.png'\n");
	}

	Color optimal_palette[PALETTE_SIZE];
	generate_wu_only_palette(framed_image, WIDTH, HEIGHT, thomson_palette, optimal_palette);

	// Trouve la palette dans l'espace de couleur thomson
	// find_closest_thomson_palette(mo5_palette, thomson_palette, palette);
	 find_closest_thomson_palette(optimal_palette, thomson_palette, palette);

	DitheredPixel *dithered_image = (DitheredPixel *)malloc(sizeof(DitheredPixel) * WIDTH * HEIGHT);
	if (!dithered_image) {
		printf("Erreur: Impossible d'allouer la mémoire pour l'image ditherée.\n");
		stbi_image_free(original_image);
		return EXIT_FAILURE;
	}


	// --- Appel de la NOUVELLE fonction de dithering avec propagation intelligente ---
	block_dithering_thomson_smart_propagation(framed_image, dithered_image, WIDTH, HEIGHT, COLOR_COMP, palette, FS_BURKES);

	// --- Vérification finale (devrait toujours être 0 violations) ---
	verify_color_clash(dithered_image, WIDTH, HEIGHT);

	unsigned char *output_image_data = (unsigned char *)malloc(width * height * COLOR_COMP);
	if (!output_image_data) {
		printf("Erreur: Impossible d'allouer la mémoire pour l'image de sortie.\n");
		free(dithered_image);
		stbi_image_free(original_image);
		return EXIT_FAILURE;
	}

	for (int y = 0; y < HEIGHT; ++y) {
		for (int x = 0; x < WIDTH; ++x) {
			int output_pixel_idx = (y * WIDTH + x) * COLOR_COMP;
			Color dithered_color = palette[dithered_image[y * WIDTH + x].palette_idx];
			output_image_data[output_pixel_idx] = dithered_color.r;
			output_image_data[output_pixel_idx + 1] = dithered_color.g;
			output_image_data[output_pixel_idx + 2] = dithered_color.b;
		}
	}

	// --- Nombre de couleurs
	long num_unique_colors = count_unique_colors_hashed(output_image_data, WIDTH, HEIGHT);
	printf("Nombre de couleurs %d\n", (int)num_unique_colors);

	// --- Image rgb ---
	if (!stbi_write_png("clash.png", WIDTH, HEIGHT, 3, output_image_data, WIDTH * 3)) {
		printf("Erreur: Impossible d'écrire l'image PNG '%s'. Tentative en BMP...\n", "clash.png");
	} else {
		printf("Image sauvée avec succès au format PNG: '%s'\n", "clash.png");
	}

	// --- Image TO-SNAP ---
	save_as_to_snap("CLASH", output_image_data, thomson_palette, palette);

	// Ajout dans une k7
	FILE *fick7 = fopen("clash.k7", "wb");
	ajouterFichier(fick7, "CLASH.MAP");

	stbi_image_free(original_image);
	free(resized_image);
	free(framed_image);
	return 0;
}
