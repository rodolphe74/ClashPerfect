#include "dither.h"
#include "thomson.h"
#include "image.h"
#include <exoquant.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

void block_dithering_thomson_smart_propagation(const unsigned char *original_image, DitheredPixel *dithered_image,
											   int width, int height, int original_channels, Color pal[16])
{
	// Alloue de la mémoire pour une version flottante de l'image (pour l'accumulation d'erreur)
	// C'est ici que l'erreur va se propager D'UN BLOC À L'AUTRE.
	double *image_float = (double *)malloc(width * height * 3 * sizeof(double));
	if (!image_float) {
		printf("Erreur: Impossible d'allouer la mémoire pour l'image flottante.\n");
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
						double error1_sq = color_distance_sq(effective_px_color, pal[i]);
						double error2_sq = color_distance_sq(effective_px_color, pal[j]);
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
				double dist1_sq = color_distance_sq(old_color_effective, pal[best_color_idx1]);
				double dist2_sq = color_distance_sq(old_color_effective, pal[best_color_idx2]);

				if (dist1_sq < dist2_sq) {
					final_pixel_palette_idx = best_color_idx1;
				} else {
					final_pixel_palette_idx = best_color_idx2;
				}

				// Assigner la couleur dithered
				dithered_image[y * width + current_x].palette_idx = final_pixel_palette_idx;
				Color new_color_quantized = pal[final_pixel_palette_idx];

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
				printf("VIOLATION DÉTECTÉE: Bloc à (%d, %d) contient %d couleurs uniques.\n", x_block_start, y,
					   color_count);
			}
		}
	}

	if (all_respected) {
		printf("RÉUSSITE : Toutes les contraintes de 2 couleurs par bloc sont respectées. (0 violations)\n");
	} else {
		printf("ATTENTION : %d blocs ne respectent PAS la contrainte de 2 couleurs. "
			   "Ceci est inattendu avec l'algorithme actuel et pourrait indiquer une erreur de logique.\n",
			   violations_count);
	}
	printf("-----------------------------------------------------------------\n");
	return all_respected;
}

void find_rgb_palette(const unsigned char *original_image, Color pal[16])
{
	exq_data *eq = exq_init();
	exq_no_transparency(eq);
	unsigned char *converted_image = NULL;
	converted_image = convert_rgb_to_rgba(original_image, WIDTH, HEIGHT);
	exq_feed(eq, converted_image, WIDTH * HEIGHT);
	exq_quantize_ex(eq, PALETTE_SIZE, 1);
	unsigned char p[16][4];
	exq_get_palette(eq, (unsigned char *)p, PALETTE_SIZE);

	for (int i = 0; i < PALETTE_SIZE; i++) {
		pal[i].r = p[i][0];
		pal[i].g = p[i][1];
		pal[i].b = p[i][2];
		printf("rgb[%d]=%d,%d,%d\n", i, p[i][0], p[i][1], p[i][2]);
	}

	free(converted_image);
	exq_free(eq);
}










// --- Auxiliary Functions for Wu's Algorithm (already provided previously) ---

static int find_closest_unique_thomson_color_idx(unsigned char target_r, unsigned char target_g, unsigned char target_b,
												 const Color thomson_palette[NUM_THOMSON_COLORS],
												 const bool *is_thomson_color_used)
{
	double min_dist_sq = -1.0;
	int closest_idx = -1;
	Color target_color_struct = {target_r, target_g, target_b, 0}; // Dummy thomson_idx for distance calculation

	for (int i = 0; i < NUM_THOMSON_COLORS; i++) {
		if (is_thomson_color_used[i]) {
			continue; // Skip Thomson colors already used in the generated palette
		}
		double dist_sq = color_distance_sq(target_color_struct, thomson_palette[i]);
		if (closest_idx == -1 || dist_sq < min_dist_sq) {
			min_dist_sq = dist_sq;
			closest_idx = i;
		}
	}
	return closest_idx;
}

// Calculates the "volume" (total pixel count) of a given color cube from moment tables.
static long volume(Cube *cube)
{
	return vwt[cube->r1][cube->g1][cube->b1] - vwt[cube->r1][cube->g1][cube->b0] - vwt[cube->r1][cube->g0][cube->b1] +
		   vwt[cube->r1][cube->g0][cube->b0] - vwt[cube->r0][cube->g1][cube->b1] + vwt[cube->r0][cube->g1][cube->b0] +
		   vwt[cube->r0][cube->g0][cube->b1] - vwt[cube->r0][cube->g0][cube->b0];
}

// Calculates the "moment" (sum of color component values) for a given color cube and component.
static long moment(Cube *cube, char component)
{
	long result;
	if (component == 'r') {
		result = vmr[cube->r1][cube->g1][cube->b1] - vmr[cube->r1][cube->g1][cube->b0] -
				 vmr[cube->r1][cube->g0][cube->b1] + vmr[cube->r1][cube->g0][cube->b0] -
				 vmr[cube->r0][cube->g1][cube->b1] + vmr[cube->r0][cube->g1][cube->b0] +
				 vmr[cube->r0][cube->g0][cube->b1] - vmr[cube->r0][cube->g0][cube->b0];
	} else if (component == 'g') {
		result = vmg[cube->r1][cube->g1][cube->b1] - vmg[cube->r1][cube->g1][cube->b0] -
				 vmg[cube->r1][cube->g0][cube->b1] + vmg[cube->r1][cube->g0][cube->b0] -
				 vmg[cube->r0][cube->g1][cube->b1] + vmg[cube->r0][cube->g1][cube->b0] +
				 vmg[cube->r0][cube->g0][cube->b1] - vmg[cube->r0][cube->g0][cube->b0];
	} else { // 'b'
		result = vmb[cube->r1][cube->g1][cube->b1] - vmb[cube->r1][cube->g1][cube->b0] -
				 vmb[cube->r1][cube->g0][cube->b1] + vmb[cube->r1][cube->g0][cube->b0] -
				 vmb[cube->r0][cube->g1][cube->b1] + vmb[cube->r0][cube->g1][cube->b0] +
				 vmb[cube->r0][cube->g0][cube->b1] - vmb[cube->r0][cube->g0][cube->b0];
	}
	return result;
}

// Calculates the variance of colors within a given cube using moment tables.
static double variance(Cube *cube)
{
	long vol = volume(cube);
	if (vol == 0) return 0.0; // Avoid division by zero for empty cubes

	long R = moment(cube, 'r');
	long G = moment(cube, 'g');
	long B = moment(cube, 'b');

	// Sum of squares of all color components within the cube
	double sum_sq = m2[cube->r1][cube->g1][cube->b1] - m2[cube->r1][cube->g1][cube->b0] -
					m2[cube->r1][cube->g0][cube->b1] + m2[cube->r1][cube->g0][cube->b0] -
					m2[cube->r0][cube->g1][cube->b1] + m2[cube->r0][cube->g1][cube->b0] +
					m2[cube->r0][cube->g0][cube->b1] - m2[cube->r0][cube->g0][cube->b0];

	// Variance formula: E[X^2] - (E[X])^2
	// Here: Sum(X^2) - (Sum(X))^2 / N
	return sum_sq - ((double)R * R + (double)G * G + (double)B * B) / vol;
}

// --- Main Palette Generation Function (Wu Only) ---

/**
 * @brief Generates a PALETTE_SIZE color palette entirely based on dominant colors
 * extracted from the image using Wu's algorithm. All generated colors are mapped
 * ("snapped") to the closest unique Thomson palette color.
 *
 * @param image_data A pointer to the raw RGB pixel data of the image (e.g., from an image loader).
 * @param image_width The width of the image in pixels.
 * @param image_height The height of the image in pixels.
 * @param thomson_palette A constant array containing the NUM_THOMSON_COLORS available Thomson colors.
 * @param generated_palette An array of Color structs (of size PALETTE_SIZE) to store the result.
 */
void generate_wu_only_palette(const unsigned char *image_data, int image_width, int image_height,
							  const Color thomson_palette[NUM_THOMSON_COLORS], Color generated_palette[PALETTE_SIZE])
{

	long num_pixels = (long)image_width * image_height;
	const Pixel *pixels = (const Pixel *)image_data; // Cast for easier pixel access

	// is_thomson_color_used_in_generated_palette: Tracks which Thomson palette colors are already chosen.
	// This ensures that each chosen Thomson color is unique in the final palette.
	bool is_thomson_color_used_in_generated_palette[NUM_THOMSON_COLORS] = {false};

	// temp_ideal_colors_pool: Temporarily stores the "ideal" colors calculated by Wu.
	WuIdealColor temp_ideal_colors_pool[PALETTE_SIZE]; // Wu will generate PALETTE_SIZE ideal colors.
	int current_ideal_pool_count = 0;				   // Counter for the number of ideal colors currently in the pool

	printf("Starting Wu-only palette generation (target %d colors from image dominance)...\n", PALETTE_SIZE);

	// --- PHASE 1: (REMOVED) No forced Black and White colors in this version. ---
	// All colors will be derived from Wu's algorithm.

	// --- PHASE 2: Build the 3D Color Histogram and Moment Tables for Wu's Algorithm ---
	printf("\n--- Phase 2: Building 3D Color Histogram and Moment Tables (Wu Algorithm Preparation) ---\n");

	// Reset Wu's moment tables for a clean start with the current image data.
	for (int r = 0; r < HIST_SIZE_DIM; r++) {
		for (int g = 0; g < HIST_SIZE_DIM; g++) {
			for (int b = 0; b < HIST_SIZE_DIM; b++) {
				vwt[r][g][b] = 0;
				vmr[r][g][b] = 0;
				vmg[r][g][b] = 0;
				vmb[r][g][b] = 0;
				m2[r][g][b] = 0.0;
			}
		}
	}
	printf("  Moment tables reset to zero.\n");

	// Populate the histogram and calculate initial moments from image pixels.
	// The 'factor' maps a 0-255 RGB value to an index between 0 and THOMSON_LEVELS_PER_DIM-1 (i.e., 0-15).
	double factor = (double)THOMSON_LEVELS_PER_DIM / MAX_COLOR_COMPONENT_VALUE; // 16.0 / 256.0 = 0.0625
	printf("  Mapping pixel values to %d bins per dimension (factor %.4f).\n", THOMSON_LEVELS_PER_DIM, factor);

	for (long i = 0; i < num_pixels; i++) {
		int r_idx = (int)(pixels[i].r * factor);
		int g_idx = (int)(pixels[i].g * factor);
		int b_idx = (int)(pixels[i].b * factor);

		// Ensure indices stay within valid bounds (0 to THOMSON_LEVELS_PER_DIM - 1, which is 0 to 15).
		if (r_idx >= THOMSON_LEVELS_PER_DIM) r_idx = THOMSON_LEVELS_PER_DIM - 1;
		if (g_idx >= THOMSON_LEVELS_PER_DIM) g_idx = THOMSON_LEVELS_PER_DIM - 1;
		if (b_idx >= THOMSON_LEVELS_PER_DIM) b_idx = THOMSON_LEVELS_PER_DIM - 1;

		vwt[r_idx][g_idx][b_idx]++;
		vmr[r_idx][g_idx][b_idx] += pixels[i].r;
		vmg[r_idx][g_idx][b_idx] += pixels[i].g;
		vmb[r_idx][g_idx][b_idx] += pixels[i].b;
		m2[r_idx][g_idx][b_idx] +=
			(double)pixels[i].r * pixels[i].r + (double)pixels[i].g * pixels[i].g + (double)pixels[i].b * pixels[i].b;
	}
	printf("  Initial histogram population and moment calculation complete.\n");

	// Compute rectangular moments (prefix sums) for efficient cube calculations.
	for (int r = 0; r < HIST_SIZE_DIM; r++) {
		for (int g = 0; g < HIST_SIZE_DIM; g++) {
			for (int b = 0; b < HIST_SIZE_DIM; b++) {
				if (r > 0) {
					vwt[r][g][b] += vwt[r - 1][g][b];
					vmr[r][g][b] += vmr[r - 1][g][b];
					vmg[r][g][b] += vmg[r - 1][g][b];
					vmb[r][g][b] += vmb[r - 1][g][b];
					m2[r][g][b] += m2[r - 1][g][b];
				}
				if (g > 0) {
					vwt[r][g][b] += vwt[r][g - 1][b];
					vmr[r][g][b] += vmr[r][g - 1][b];
					vmg[r][g][b] += vmg[r][g - 1][b];
					vmb[r][g][b] += vmb[r][g - 1][b];
					m2[r][g][b] += m2[r][g - 1][b];
				}
				if (b > 0) {
					vwt[r][g][b] += vwt[r][g][b - 1];
					vmr[r][g][b] += vmr[r][g][b - 1];
					vmg[r][g][b] += vmg[r][g][b - 1];
					vmb[r][g][b] += vmb[r][g][b - 1];
					m2[r][g][b] += m2[r][g][b - 1];
				}
				if (r > 0 && g > 0) {
					vwt[r][g][b] -= vwt[r - 1][g - 1][b];
					vmr[r][g][b] -= vmr[r - 1][g - 1][b];
					vmg[r][g][b] -= vmg[r - 1][g - 1][b];
					vmb[r][g][b] -= vmb[r - 1][g - 1][b];
					m2[r][g][b] -= m2[r - 1][g - 1][b];
				}
				if (r > 0 && b > 0) {
					vwt[r][g][b] -= vwt[r - 1][g][b - 1];
					vmr[r][g][b] -= vmr[r - 1][g][b - 1];
					vmg[r][g][b] -= vmg[r - 1][g][b - 1];
					vmb[r][g][b] -= vmb[r - 1][g][b - 1];
					m2[r][g][b] -= m2[r - 1][g][b - 1];
				}
				if (g > 0 && b > 0) {
					vwt[r][g][b] -= vwt[r][g - 1][b - 1];
					vmr[r][g][b] -= vmr[r][g - 1][b - 1];
					vmg[r][g][b] -= vmg[r][g - 1][b - 1];
					vmb[r][g][b] -= vmb[r][g - 1][b - 1];
					m2[r][g][b] -= m2[r][g - 1][b - 1];
				}
				if (r > 0 && g > 0 && b > 0) {
					vwt[r][g][b] += vwt[r - 1][g - 1][b - 1];
					vmr[r][g][b] += vmr[r - 1][g - 1][b - 1];
					vmg[r][g][b] += vmg[r - 1][g - 1][b - 1];
					vmb[r][g][b] += vmb[r - 1][g - 1][b - 1];
					m2[r][g][b] += m2[r - 1][g - 1][b - 1];
				}
			}
		}
	}
	printf("  Rectangular moments computation complete.\n");
	printf("--- Phase 2 Finished ---\n\n");

	// --- PHASE 3: Extract PALETTE_SIZE (16) dominant colors using Wu's algorithm ---
	printf("--- Phase 3: Extracting %d Wu Dominant Colors (for full palette) ---\n", PALETTE_SIZE);
	Cube cubes[PALETTE_SIZE]; // Array to hold the cubes after splitting
	cubes[0].r0 = cubes[0].g0 = cubes[0].b0 = 0;
	cubes[0].r1 = cubes[0].g1 = cubes[0].b1 = HIST_SIZE_DIM - 1; // Initial cube covers entire histogram.
	int num_cubes = 1;											 // Start with one cube covering the entire color space

	// Iteratively split the cube that offers the largest variance reduction
	// until we have PALETTE_SIZE cubes (each representing a dominant color).
	while (num_cubes < PALETTE_SIZE) { // Loop until we have PALETTE_SIZE cubes
		double max_reduction = 0.0;
		int best_cube_idx = -1;
		int best_axis = -1; // 0=R, 1=G, 2=B
		int best_cut_point = -1;

		// Iterate through existing cubes to find the best one to split.
		for (int i = 0; i < num_cubes; i++) {
			Cube *current_cube = &cubes[i];
			if (volume(current_cube) == 0) continue; // Don't split empty cubes

			int r_range = current_cube->r1 - current_cube->r0;
			int g_range = current_cube->g1 - current_cube->g0;
			int b_range = current_cube->b1 - current_cube->b0;

			// Find the longest axis to split along (Wu's heuristic for optimal splitting)
			int current_max_range = 0;
			int current_axis_to_cut = -1;

			if (r_range >= current_max_range) {
				current_max_range = r_range;
				current_axis_to_cut = 0;
			}
			if (g_range >= current_max_range) {
				current_max_range = g_range;
				current_axis_to_cut = 1;
			}
			if (b_range >= current_max_range) {
				current_max_range = b_range;
				current_axis_to_cut = 2;
			}

			double current_best_cut_reduction = -1.0;
			int current_best_cut_point = -1;

			int min_bound = 0, max_bound = 0;
			if (current_axis_to_cut == 0) {
				min_bound = current_cube->r0;
				max_bound = current_cube->r1;
			} else if (current_axis_to_cut == 1) {
				min_bound = current_cube->g0;
				max_bound = current_cube->g1;
			} else {
				min_bound = current_cube->b0;
				max_bound = current_cube->b1;
			}

			// Iterate through all possible cut points along the chosen axis to find the one that maximizes variance
			// reduction.
			for (int p = min_bound + 1; p <= max_bound; p++) {
				Cube temp_left = *current_cube;
				Cube temp_right = *current_cube;

				if (current_axis_to_cut == 0) {
					temp_left.r1 = p;
					temp_right.r0 = p;
				} else if (current_axis_to_cut == 1) {
					temp_left.g1 = p;
					temp_right.g0 = p;
				} else {
					temp_left.b1 = p;
					temp_right.b0 = p;
				}

				long vol_left = volume(&temp_left);
				long vol_right = volume(&temp_right);
				if (vol_left == 0 || vol_right == 0) continue; // Avoid splits that create empty sub-cubes

				double reduction = variance(current_cube) - (variance(&temp_left) + variance(&temp_right));

				if (reduction > current_best_cut_reduction) {
					current_best_cut_reduction = reduction;
					current_best_cut_point = p;
				}
			}

			// Update the overall best split found across all cubes.
			if (current_best_cut_point != -1 && current_best_cut_reduction > max_reduction) {
				max_reduction = current_best_cut_reduction;
				best_cube_idx = i;
				best_axis = current_axis_to_cut;
				best_cut_point = current_best_cut_point;
			}
		}

		if (best_cube_idx == -1) {
			fprintf(stderr,
					"Wu: Failed to find a suitable cube to split. This may happen if the image has very few distinct "
					"colors. Final Wu colors found: %d\n",
					num_cubes);
			break; // No further beneficial splits can be made, stop.
		}

		Cube *cube_to_split = &cubes[best_cube_idx];
		Cube new_cube = *cube_to_split; // The new cube is a copy of the one being split

		// Apply the split: adjust bounds for both the original cube and the new one.
		if (best_axis == 0) {
			cube_to_split->r1 = best_cut_point;
			new_cube.r0 = best_cut_point;
		} else if (best_axis == 1) {
			cube_to_split->g1 = best_cut_point;
			new_cube.g0 = best_cut_point;
		} else {
			cube_to_split->b1 = best_cut_point;
			new_cube.b0 = best_cut_point;
		}

		cubes[num_cubes] = new_cube; // Add the newly created cube to the list.
		num_cubes++;
	}

	// Calculate the "ideal" RGB values for each dominant color from the resulting cubes.
	// These are the centroid colors of each final cube.
	for (int i = 0; i < num_cubes; i++) {
		Cube *current_cube = &cubes[i];
		long vol = volume(current_cube);
		if (vol > 0) {
			temp_ideal_colors_pool[current_ideal_pool_count].r_val = (double)moment(current_cube, 'r') / vol;
			temp_ideal_colors_pool[current_ideal_pool_count].g_val = (double)moment(current_cube, 'g') / vol;
			temp_ideal_colors_pool[current_ideal_pool_count].b_val = (double)moment(current_cube, 'b') / vol;
		} else {
			// Fallback for empty cubes (should be rare if image has enough colors and splits are well-chosen).
			// Assign a random color if a cube is empty.
			temp_ideal_colors_pool[current_ideal_pool_count].r_val = rand() % 256;
			temp_ideal_colors_pool[current_ideal_pool_count].g_val = rand() % 256;
			temp_ideal_colors_pool[current_ideal_pool_count].b_val = rand() % 256;
		}
		current_ideal_pool_count++;
	}
	printf("  Wu algorithm completed. Generated %d ideal dominant colors.\n", current_ideal_pool_count);
	printf("--- Phase 3 Finished ---\n\n");

	// --- PHASE 4: (REMOVED) No additional fixed colors (CMY/Grayscale) in this Wu-only version. ---
	// All palette colors will come from the Wu dominant colors.

	// --- PHASE 5: "Snap" all ideal Wu colors to unique Thomson palette colors ---
	printf("--- Phase 5: Snapping Ideal Wu Colors to Unique Thomson Palette Colors ---\n");

	// This loop fills the entire generated_palette (from index 0 to PALETTE_SIZE-1).
	for (int i = 0; i < PALETTE_SIZE; i++) {
		int closest_thomson_idx = -1;

		if (i < current_ideal_pool_count) { // If we have a Wu ideal color for this slot
			closest_thomson_idx = find_closest_unique_thomson_color_idx(
				(unsigned char)temp_ideal_colors_pool[i].r_val, (unsigned char)temp_ideal_colors_pool[i].g_val,
				(unsigned char)temp_ideal_colors_pool[i].b_val, thomson_palette,
				is_thomson_color_used_in_generated_palette);
		} else {
			// This case should ideally not happen if num_cubes (current_ideal_pool_count)
			// successfully reached PALETTE_SIZE in Phase 3.
			// However, as a safeguard, if Wu failed to find enough distinct cubes,
			// we will fill remaining slots with random unique Thomson colors.
			fprintf(stderr,
					"Warning: Wu algorithm yielded fewer than %d dominant colors. Filling remaining slots with random "
					"Thomson colors.\n",
					PALETTE_SIZE);
			Color random_fallback_color = {(unsigned char)(rand() % 256), (unsigned char)(rand() % 256),
										   (unsigned char)(rand() % 256), 0};
			closest_thomson_idx = find_closest_unique_thomson_color_idx(
				random_fallback_color.r, random_fallback_color.g, random_fallback_color.b, thomson_palette,
				is_thomson_color_used_in_generated_palette);
		}

		if (closest_thomson_idx != -1) {
			generated_palette[i] = thomson_palette[closest_thomson_idx];
			is_thomson_color_used_in_generated_palette[closest_thomson_idx] = true;
			printf("  Snapping Ideal #%d (Target: R:%.0f G:%.0f B:%.0f) -> Thomson Index %d (R:%d G:%d B:%d) at "
				   "palette[%d]\n",
				   i, temp_ideal_colors_pool[i].r_val, temp_ideal_colors_pool[i].g_val, temp_ideal_colors_pool[i].b_val,
				   thomson_palette[closest_thomson_idx].thomson_idx, thomson_palette[closest_thomson_idx].r,
				   thomson_palette[closest_thomson_idx].g, thomson_palette[closest_thomson_idx].b, i);
		} else {
			// Fallback if no more unique Thomson colors are available for any reason.
			generated_palette[i] = (Color){0, 0, 0, 0}; // Default to black
			fprintf(stderr, "Error: Could not find any unique Thomson color for slot %d. Assigning fallback (0,0,0).\n",
					i);
		}
	}
	printf("--- Phase 5 Finished ---\n\n");
	printf("Wu-only palette generation completed. Final palette size: %d\n", PALETTE_SIZE);
}