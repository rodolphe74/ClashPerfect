#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>	 // Pour fmin
#include <time.h>	 // Pour srand(time(NULL))
#include <string.h>	 // Pour memcpy()
#include <stdbool.h> // Pour utiliser 'bool'

// --- Structures ---

typedef struct {
	unsigned char r, g, b;
} Color;

typedef struct {
	int palette_idx;
} DitheredPixel;

// --- Palette Thomson 16 couleurs (ADAPTER VOS VALEURS RÉELLES !) ---
Color thomson_palette[16] = {{0x00, 0x00, 0x00}, {0x00, 0x00, 0x80}, {0x00, 0x80, 0x00}, {0x00, 0x80, 0x80},
							 {0x80, 0x00, 0x00}, {0x80, 0x00, 0x80}, {0x80, 0x80, 0x00}, {0x80, 0x80, 0x80},
							 {0xC0, 0xC0, 0xC0}, {0x00, 0x00, 0xFF}, {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF},
							 {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF}};

// --- Fonctions utilitaires ---

double color_distance_sq(Color c1, Color c2)
{
	double dr = (double)c1.r - c2.r;
	double dg = (double)c1.g - c2.g;
	double db = (double)c1.b - c2.b;
	return dr * dr + dg * dg + db * db;
}

unsigned char clamp_color_component(double val)
{
	if (val < 0.0) return 0;
	if (val > 255.0) return 255;
	return (unsigned char)val;
}

// --- Fonction de Dithering avec Propagation Intelligente entre les Blocs ---
void block_dithering_thomson_smart_propagation(const unsigned char *original_image, DitheredPixel *dithered_image,
											   int width, int height, int original_channels)
{
	printf("Début du Dithering Bloqué avec Propagation d'Erreur Intelligente...\n");

	// Alloue de la mémoire pour une version flottante de l'image (pour l'accumulation d'erreur)
	// C'est ici que l'erreur va se propager D'UN BLOC À L'AUTRE.
	double *image_float = (double *)malloc(width * height * 3 * sizeof(double));
	if (!image_float) {
		fprintf(stderr, "Erreur: Impossible d'allouer la mémoire pour l'image flottante.\n");
		exit(EXIT_FAILURE);
	}

	// Initialise l'image flottante avec les données de l'image originale.
	for (int i = 0; i < width * height * 3; ++i) {
		image_float[i] = (double)original_image[i];
	}

	for (int y = 0; y < height; ++y) {
		for (int x_block_start = 0; x_block_start < width; x_block_start += 8) {

			// A. Préparer les "couleurs effectives" du bloc (original + erreur propagée)
			Color block_effective_colors[8]; // Couleurs des pixels du bloc, incluant l'erreur flottante
			int current_block_size = 0;

			for (int dx = 0; dx < 8; ++dx) {
				int current_x = x_block_start + dx;
				if (current_x >= width) break;

				int float_idx = (y * width + current_x) * 3;
				block_effective_colors[dx].r = (unsigned char)clamp_color_component(image_float[float_idx]);
				block_effective_colors[dx].g = (unsigned char)clamp_color_component(image_float[float_idx + 1]);
				block_effective_colors[dx].b = (unsigned char)clamp_color_component(image_float[float_idx + 2]);
				current_block_size++;
			}

			if (current_block_size == 0) continue;

			// B. Trouver les 2 meilleures couleurs de palette pour ce bloc, basées sur les couleurs effectives
			// Cette étape est cruciale : elle utilise les couleurs "pré-ditherées" (avec erreur accumulée)
			// pour faire un meilleur choix de palette.
			double min_total_error_sq = -1.0;
			int best_color_idx1 = -1;
			int best_color_idx2 = -1;

			for (int i = 0; i < 16; ++i) {
				for (int j = i; j < 16; ++j) { // j=i pour permettre le cas où une seule couleur est optimale
					double current_pair_total_error_sq = 0.0;
					for (int k = 0; k < current_block_size; ++k) {
						Color effective_px_color = block_effective_colors[k];
						double error1_sq = color_distance_sq(effective_px_color, thomson_palette[i]);
						double error2_sq = color_distance_sq(effective_px_color, thomson_palette[j]);
						current_pair_total_error_sq += fmin(error1_sq, error2_sq);
					}

					if (min_total_error_sq < 0 || current_pair_total_error_sq < min_total_error_sq) {
						min_total_error_sq = current_pair_total_error_sq;
						best_color_idx1 = i;
						best_color_idx2 = j;
					}
				}
			}

			// Fallback (ne devrait pas être nécessaire si la palette n'est pas vide)
			if (best_color_idx1 == -1) {
				best_color_idx1 = 0;
				best_color_idx2 = 1;
			}

			// C. Dithering Floyd-Steinberg à l'intérieur du bloc et propagation de l'erreur
			// Cette partie ressemble à un Floyd-Steinberg classique, mais les couleurs cibles
			// sont limitées à best_color_idx1 et best_color_idx2.
			for (int dx = 0; dx < current_block_size; ++dx) {
				int current_x = x_block_start + dx;
				int float_idx = (y * width + current_x) * 3;

				// Couleur actuelle du pixel flottant (incluant l'erreur propagée)
				Color old_color_effective = {(unsigned char)clamp_color_component(image_float[float_idx]),
											 (unsigned char)clamp_color_component(image_float[float_idx + 1]),
											 (unsigned char)clamp_color_component(image_float[float_idx + 2])};

				// Quantifier à la couleur de palette la plus proche PARMI LES DEUX CHOISIES POUR LE BLOC
				int final_pixel_palette_idx;
				double dist1_sq = color_distance_sq(old_color_effective, thomson_palette[best_color_idx1]);
				double dist2_sq = color_distance_sq(old_color_effective, thomson_palette[best_color_idx2]);

				if (dist1_sq < dist2_sq) {
					final_pixel_palette_idx = best_color_idx1;
				} else {
					final_pixel_palette_idx = best_color_idx2;
				}

				// Assigner la couleur dithered
				dithered_image[y * width + current_x].palette_idx = final_pixel_palette_idx;
				Color new_color_quantized = thomson_palette[final_pixel_palette_idx];

				// Calculer l'erreur de quantification
				double error_r = (double)old_color_effective.r - new_color_quantized.r;
				double error_g = (double)old_color_effective.g - new_color_quantized.g;
				double error_b = (double)old_color_effective.b - new_color_quantized.b;

				// Propager l'erreur aux voisins DANS L'IMAGE FLOTTANTE GLOBALE
				// C'est ici que l'intelligence réside : l'erreur est propagée "normalement"
				// mais c'est la phase de CHOIX DE PALETTE DU BLOC SUIVANT qui s'adapte.

				// (x+1, y) - à droite (dans le même bloc ou bloc voisin)
				if (current_x + 1 < width) {
					int neighbor_float_idx = (y * width + (current_x + 1)) * 3;
					image_float[neighbor_float_idx] += error_r * 7.0 / 16.0;
					image_float[neighbor_float_idx + 1] += error_g * 7.0 / 16.0;
					image_float[neighbor_float_idx + 2] += error_b * 7.0 / 16.0;
				}

				// (x-1, y+1) - en bas à gauche (bloc voisin en dessous)
				if (current_x - 1 >= 0 && y + 1 < height) {
					int neighbor_float_idx = ((y + 1) * width + (current_x - 1)) * 3;
					image_float[neighbor_float_idx] += error_r * 3.0 / 16.0;
					image_float[neighbor_float_idx + 1] += error_g * 3.0 / 16.0;
					image_float[neighbor_float_idx + 2] += error_b * 3.0 / 16.0;
				}

				// (x, y+1) - en bas (bloc voisin en dessous)
				if (y + 1 < height) {
					int neighbor_float_idx = ((y + 1) * width + current_x) * 3;
					image_float[neighbor_float_idx] += error_r * 5.0 / 16.0;
					image_float[neighbor_float_idx + 1] += error_g * 5.0 / 16.0;
					image_float[neighbor_float_idx + 2] += error_b * 5.0 / 16.0;
				}

				// (x+1, y+1) - en bas à droite (bloc voisin en dessous)
				if (current_x + 1 < width && y + 1 < height) {
					int neighbor_float_idx = ((y + 1) * width + (current_x + 1)) * 3;
					image_float[neighbor_float_idx] += error_r * 1.0 / 16.0;
					image_float[neighbor_float_idx + 1] += error_g * 1.0 / 16.0;
					image_float[neighbor_float_idx + 2] += error_b * 1.0 / 16.0;
				}
			}
		}
	}
	free(image_float);
	printf("Dithering Bloqué avec Propagation d'Erreur Intelligente terminé.\n");
}

// --- Fonction de Vérification du Color Clash (essentielle) ---
// Cette fonction reste la même et est cruciale pour valider que l'algorithme respecte la contrainte.
bool verify_color_clash(const DitheredPixel *dithered_image, int width, int height)
{
	printf("\n--- Vérification finale du respect de la contrainte de 2 couleurs par bloc ---\n");
	bool all_respected = true;
	int violations_count = 0;

	for (int y = 0; y < height; ++y) {
		for (int x_block_start = 0; x_block_start < width; x_block_start += 8) {
			int unique_colors_in_block[16] = {0};
			int color_count = 0;

			for (int dx = 0; dx < 8; ++dx) {
				int current_x = x_block_start + dx;
				if (current_x >= width) break;

				int palette_idx = dithered_image[y * width + current_x].palette_idx;
				if (unique_colors_in_block[palette_idx] == 0) {
					unique_colors_in_block[palette_idx] = 1;
					color_count++;
				}
			}

			if (color_count > 2) {
				all_respected = false;
				violations_count++;
				fprintf(stderr, "VIOLATION DÉTECTÉE: Bloc à (%d, %d) contient %d couleurs uniques.\n", x_block_start, y,
						color_count);
			}
		}
	}

	if (all_respected) {
		printf("RÉUSSITE : Toutes les contraintes de 2 couleurs par bloc sont respectées. (0 violations)\n");
	} else {
		fprintf(stderr,
				"ATTENTION : %d blocs ne respectent PAS la contrainte de 2 couleurs. "
				"Ceci est inattendu avec l'algorithme actuel et pourrait indiquer une erreur de logique.\n",
				violations_count);
	}
	printf("-----------------------------------------------------------------\n");
	return all_respected;
}

// --- Fonction principale ---
int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Utilisation: %s <chemin_image_entree.png> <chemin_image_sortie.png>\n", argv[0]);
		return EXIT_FAILURE;
	}

	srand((unsigned int)time(NULL));

	int width, height, channels;
	unsigned char *original_image = stbi_load(argv[1], &width, &height, &channels, 3);
	if (!original_image) {
		fprintf(stderr, "Erreur: Impossible de charger l'image d'entrée '%s'. Vérifiez le chemin ou le format.\n",
				argv[1]);
		return EXIT_FAILURE;
	}

	printf("Image chargée: %s (%dx%d pixels, %d canaux d'origine)\n", argv[1], width, height, channels);

	DitheredPixel *dithered_image = (DitheredPixel *)malloc(sizeof(DitheredPixel) * width * height);
	if (!dithered_image) {
		fprintf(stderr, "Erreur: Impossible d'allouer la mémoire pour l'image ditherée.\n");
		stbi_image_free(original_image);
		return EXIT_FAILURE;
	}

	// --- Appel de la NOUVELLE fonction de dithering avec propagation intelligente ---
	block_dithering_thomson_smart_propagation(original_image, dithered_image, width, height, 3);

	// --- Vérification finale (devrait toujours être 0 violations) ---
	verify_color_clash(dithered_image, width, height);

	unsigned char *output_image_data = (unsigned char *)malloc(width * height * 3);
	if (!output_image_data) {
		fprintf(stderr, "Erreur: Impossible d'allouer la mémoire pour l'image de sortie.\n");
		free(dithered_image);
		stbi_image_free(original_image);
		return EXIT_FAILURE;
	}

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int output_pixel_idx = (y * width + x) * 3;
			Color dithered_color = thomson_palette[dithered_image[y * width + x].palette_idx];
			output_image_data[output_pixel_idx] = dithered_color.r;
			output_image_data[output_pixel_idx + 1] = dithered_color.g;
			output_image_data[output_pixel_idx + 2] = dithered_color.b;
		}
	}

	if (!stbi_write_png(argv[2], width, height, 3, output_image_data, width * 3)) {
		fprintf(stderr, "Erreur: Impossible d'écrire l'image PNG '%s'. Tentative en BMP...\n", argv[2]);
		char bmp_path[256];
		if (snprintf(bmp_path, sizeof(bmp_path), "%s.bmp", argv[2]) >= sizeof(bmp_path)) {
			fprintf(stderr, "Nom de fichier BMP résultant trop long.\n");
		} else {
			if (!stbi_write_bmp(bmp_path, width, height, 3, output_image_data)) {
				fprintf(stderr,
						"Erreur: Échec de l'écriture en BMP également. Vérifiez les permissions ou le chemin.\n");
			} else {
				printf("Image sauvée avec succès au format BMP: '%s'\n", bmp_path);
			}
		}
	} else {
		printf("Image sauvée avec succès au format PNG: '%s'\n", argv[2]);
	}

	stbi_image_free(original_image);
	free(dithered_image);
	free(output_image_data);

	return EXIT_SUCCESS;
}
