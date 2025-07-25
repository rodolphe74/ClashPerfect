#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
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
#include "palettes.h"
#include "matrix.h"
#include "k7.h"

void usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: clash <nom_fichier> [-d<chiffre>] [-m<chiffre>]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "-d<chiffre> : matrice de dithering\n");
	fprintf(stderr, "  0=Standard\n");
	fprintf(stderr, "  1=Jarvis\n");
	fprintf(stderr, "  2=Zhigang\n");
	fprintf(stderr, "  3=Shiau\n");
	fprintf(stderr, "  4=Shiau 2\n");
	fprintf(stderr, "  5=Stucki\n");
	fprintf(stderr, "  6=Burkes\n");
	fprintf(stderr, "  7=Sierra\n");
	fprintf(stderr, "  8=Atkinson\n");
	fprintf(stderr, "  9=Ostromoukhov\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "-m<chiffre> : machine\n");
	fprintf(stderr, "  0=MO5\n");
	fprintf(stderr, "  1=MO6\n");
}

int main(int argc, char *argv[])
{
	Color thomson_palette[4096];
	init_thomson_palette(thomson_palette);
	Color palette[PALETTE_SIZE];



	int opt;
	char *nom_fichier = NULL;
	int val_d = -1; // Initialisé à -1 pour indiquer qu'il n'a pas été défini
	int val_m = -1; // Initialisé à -1 pour indiquer qu'il n'a pas été défini
	int pal = 0;
	char *pal_name = NULL;

	// Chaîne d'options : "d:m:" signifie que -d prend un argument et -m prend un argument
	while ((opt = getopt(argc, argv, "d:m:p:")) != -1) {
		switch (opt) {
		case 'd':
			val_d = atoi(optarg); // optarg contient la chaîne de l'argument (ex: "0")
			if (val_d < 0 || val_d > 9) {
				usage();
				return 1;
			}
			break;
		case 'm':
			val_m = atoi(optarg);
			if (val_m < 0 || val_m > 1) {
				usage();
				return 1;
			};
			break;
		case 'p':
			pal_name = optarg;
			break;
		case '?': // getopt renvoie '?' si une option est inconnue ou un argument manque
			usage();
			return 1; // Code d'erreur
		}
	}

	// Après la boucle getopt, optind est l'indice du premier argument non-optionnel.
	// Dans votre cas, ce sera le nom de fichier.
	if (optind < argc) {
		nom_fichier = argv[optind]; // Le premier argument non-optionnel est notre nom de fichier
	} else {
		fprintf(stderr, "Erreur: Le nom de fichier est manquant.\n");
		usage();
		return 1;
	}

	// Vérifier si toutes les options requises ont été définies (si elles sont obligatoires)
	if (val_d == -1) {
		val_d = 0;
	}
	if (val_m == -1) {
		val_m = 0;
	}

	printf("arguments: %s %d %d\n", nom_fichier, val_d, val_m);


	int width, height, channels;
	//unsigned char *original_image = stbi_load(argv[1], &width, &height, &channels, COLOR_COMP);
	unsigned char *original_image = stbi_load(nom_fichier, &width, &height, &channels, COLOR_COMP);
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

	if (pal_name) {
		int chosen_index = 0;
		Color chosen[16];
		for (int i = 0; i < NUM_PALETTES; i++) {
			if (strcmp(pal_name, palette_table[i].name) == 0) {
				chosen_index = i;
				break;
			}
		}
		for (int i = 0; i < 16; i++) {
			chosen[i] = palette_table[chosen_index].palette[i];
		}
		find_closest_thomson_palette(chosen, thomson_palette, palette);
	} else if (val_m == 1) {
		generate_palette_wu_thomson_aware(framed_image, WIDTH, HEIGHT, thomson_palette, optimal_palette);
		find_closest_thomson_palette(optimal_palette, thomson_palette, palette);
	} else {
		find_closest_thomson_palette(mo5_palette, thomson_palette, palette);
	}


	DitheredPixel *dithered_image = (DitheredPixel *)malloc(sizeof(DitheredPixel) * WIDTH * HEIGHT);
	if (!dithered_image) {
		printf("Erreur: Impossible d'allouer la mémoire pour l'image ditherée.\n");
		stbi_image_free(original_image);
		return EXIT_FAILURE;
	}


	// --- Appel de la NOUVELLE fonction de dithering avec propagation intelligente ---
	block_dithering_thomson_smart_propagation(framed_image, dithered_image, WIDTH, HEIGHT, COLOR_COMP, palette,
											  val_d == 9 ? NULL : floyd_matrix[val_d].matrix);

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
		printf("clash.png créé\n");
	}

	// --- Image TO-SNAP ---
	IntVector pixels, colors;
	init_vector(&pixels);
	init_vector(&colors);
	save_as_to_snap("CLASH", output_image_data, thomson_palette, palette, &pixels, &colors);
	printf("CLASH.MAP créé\n");

	// --- Création des fichiers binaires couleur et forme MO5
	uint8_t header[] = {0x00, 0x1F, 0x40, 0x00, 0x00};
	uint8_t footer[] = {0xFF, 0x00, 0x00, 0x00, 0x00};
	uint8_t *c = (uint8_t *)malloc(colors.size);
	if (c)
		for (int i = 0; i < colors.size; i++) {
			c[i] = colors.data[i];
		}
	FILE *fc = fopen("COLORS.BIN", "wb");
	fwrite(header, 1, 5, fc);
	if (c) fwrite(c, 1, colors.size, fc);
	fwrite(footer, 1, 5, fc);
	fclose(fc);
	free(c);
	uint8_t *p = (uint8_t *)malloc(pixels.size);
	if (p)
		for (int i = 0; i < pixels.size; i++) {
			p[i] = pixels.data[i];
		}
	FILE *fp = fopen("PIXELS.BIN", "wb");
	fwrite(header, 1, 5, fc);
	if (p) fwrite(p, 1, pixels.size, fp);
	fwrite(footer, 1, 5, fc);
	fclose(fp);
	free(p);

	// --- Ajout dans une k7 ---
 	FILE *fick7 = fopen("clash.k7", "wb");
	ajouterFichier(fick7, "CLASH.MAP");
	ajouterFichier(fick7, "PIXELS.BIN");
	ajouterFichier(fick7, "COLORS.BIN");
	fclose(fick7);
	printf("clash.k7 créé\n");

	free_vector(&pixels);
	free_vector(&colors);
	stbi_image_free(original_image);
	free(resized_image);
	free(framed_image);
	return 0;
}
