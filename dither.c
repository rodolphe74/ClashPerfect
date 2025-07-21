#include "dither.h"
#include "thomson.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

uint32_t thomson_histogram[NUM_THOMSON_COLORS];

double distance_squared(unsigned char r1, unsigned char g1, unsigned char b1, unsigned char r2, unsigned char g2,
							   unsigned char b2)
{
	long dr = r1 - r2;
	long dg = g1 - g2;
	long db = b1 - b2;
	return (double)(dr * dr + dg * dg + db * db);
}

double color_distance_sq(Color c1, Color c2)
{
	double dr = (double)c1.r - c2.r;
	double dg = (double)c1.g - c2.g;
	double db = (double)c1.b - c2.b;
	return dr * dr + dg * dg + db * db;
}

// Trouve l'index de la couleur Thomson la plus proche d'une couleur RGB donnée
// Cette version ne gère PAS l'unicité des couleurs Thomson par elle-même.
// Elle est utilisée pour le pré-snapping de chaque pixel ou pour trouver la couleur Thomson la plus proche d'un
// centroïde idéal.
int find_closest_thomson_idx(unsigned char r, unsigned char g, unsigned char b,
									const Color thomson_pal[NUM_THOMSON_COLORS], const bool *current_used_flags)
{
	double min_dist_sq = -1.0;
	int closest_idx = -1;

	for (int i = 0; i < NUM_THOMSON_COLORS; i++) {
		// Optionnel: Ignorer les couleurs Thomson déjà "utilisées" par d'autres centroïdes pour l'unicité
		// lors de la mise à jour des centroïdes. Pour le pré-snapping initial des pixels, ce flag n'est pas utilisé.
		if (current_used_flags != NULL && current_used_flags[i]) {
			continue;
		}

		double dist_sq = distance_squared(r, g, b, thomson_pal[i].r, thomson_pal[i].g, thomson_pal[i].b);
		if (closest_idx == -1 || dist_sq < min_dist_sq) {
			min_dist_sq = dist_sq;
			closest_idx = i;
		}
	}
	return closest_idx;
}







// Fonction pour calculer les moments (sommes et variances) pour une boîte
// MAINTENANT AVEC thomson_pal_source EN PARAMÈTRE
static void calculate_box_moments(WuBox *box, const Color thomson_pal_source[NUM_THOMSON_COLORS])
{
	box->sum_r = 0;
	box->sum_g = 0;
	box->sum_b = 0;
	box->pixel_count = 0;
	box->variance = 0.0;

	if (box->min_idx > box->max_idx) {
		return;
	}

	for (int i = box->min_idx; i <= box->max_idx; i++) {
		uint32_t count_i = thomson_histogram[i];
		if (count_i > 0) {
			box->sum_r += thomson_pal_source[i].r * count_i;
			box->sum_g += thomson_pal_source[i].g * count_i;
			box->sum_b += thomson_pal_source[i].b * count_i;
			box->pixel_count += count_i;
		}
	}

	if (box->pixel_count == 0) {
		return;
	}

	Color avg_color;
	avg_color.r = (uint8_t)round((double)box->sum_r / box->pixel_count);
	avg_color.g = (uint8_t)round((double)box->sum_g / box->pixel_count);
	avg_color.b = (uint8_t)round((double)box->sum_b / box->pixel_count);

	double current_box_variance_sum_sq = 0.0;
	for (int i = box->min_idx; i <= box->max_idx; i++) {
		uint32_t count_i = thomson_histogram[i];
		if (count_i > 0) {
			current_box_variance_sum_sq +=
				distance_squared(thomson_pal_source[i].r, thomson_pal_source[i].g, thomson_pal_source[i].b, avg_color.r,
								 avg_color.g, avg_color.b) *
				count_i;
		}
	}
	box->variance = current_box_variance_sum_sq;
}

// Fonction pour trouver la meilleure division d'une boîte existante
// Retourne true si une division est possible, false sinon
static bool find_best_split(WuBox *box_to_split, WuBox *new_box, const Color thomson_pal_source[NUM_THOMSON_COLORS])
{
	if (box_to_split->pixel_count == 0) {
		return false; // Cannot split an empty box
	}

	double max_variance_reduction = -1.0;
	int best_split_idx = -1;

	// We can only split along the Thomson index axis here, as it's a 1D range.
	// In a full Wu, you'd iterate through R, G, B dimensions.
	// Here, the Thomson index intrinsically represents a 3D color, so splitting
	// the index range means splitting the implicit 3D color space along some axis.

	// Iterate through all possible split points within the box
	for (int split_idx = box_to_split->min_idx; split_idx < box_to_split->max_idx; split_idx++) {
		WuBox box1, box2;

		// First sub-box
		box1.min_idx = box_to_split->min_idx;
		box1.max_idx = split_idx;
		calculate_box_moments(&box1, thomson_pal_source);

		// Second sub-box
		box2.min_idx = split_idx + 1;
		box2.max_idx = box_to_split->max_idx;
		calculate_box_moments(&box2, thomson_pal_source);

		// Calculate variance reduction
		double current_variance_reduction = box_to_split->variance - (box1.variance + box2.variance);

		if (current_variance_reduction > max_variance_reduction) {
			max_variance_reduction = current_variance_reduction;
			best_split_idx = split_idx;
		}
	}

	if (best_split_idx != -1 && max_variance_reduction > 0) {
		// Perform the split
		new_box->min_idx = best_split_idx + 1;
		new_box->max_idx = box_to_split->max_idx;
		calculate_box_moments(new_box, thomson_pal_source); // Calculate moments for the new box

		box_to_split->max_idx = best_split_idx;					 // Adjust the original box's max_idx
		calculate_box_moments(box_to_split, thomson_pal_source); // Recalculate moments for the modified original box

		return true;
	}
	return false;
}


// Étapes Clés de l'Algorithme de Wu (Adapté Thomson)
// 1. Construction de l'Histogramme Thomson-Aware :
// But : Compter la fréquence d'apparition de chaque couleur Thomson pertinente dans l'image source.
// Processus :
// Pour chaque pixel de l'image originale (en RGB 24-bits), on trouve la couleur Thomson la plus proche dans la
// thomson_palette (celle de 4096 couleurs). On incrémente un compteur (thomson_histogram) pour l'index de cette couleur
// Thomson. Résultat : Un tableau où chaque entrée thomson_histogram[i] indique combien de pixels de l'image originale
// sont les plus proches de thomson_palette[i]. C'est l'entrée de l'algorithme de Wu.

// 2. Initialisation de la Première "Boîte" :
// Concept de Boîte : Une "boîte" représente une région contiguë de l'espace colorimétrique. Dans notre cas simplifié,
// c'est une plage d'indices Thomson (par exemple, de l'index 0 à 4095 pour la première boîte). Calcul des Moments :
// Pour cette boîte initiale (et toutes les boîtes futures), on calcule des "moments" : La somme des composantes R, G, B
// de toutes les couleurs Thomson présentes dans cette boîte, pondérée par leur fréquence dans le thomson_histogram. Le
// nombre total de pixels (pixel_count) dont la couleur Thomson la plus proche est dans cette boîte. La variance des
// couleurs au sein de cette boîte. (La variance est une mesure de la dispersion des couleurs. Une boîte avec une faible
// variance contient des couleurs très similaires, une avec une haute variance contient des couleurs très différentes).

// 3. Division Itérative des Boîtes :
// Objectif : Atteindre le nombre désiré de couleurs dans la palette finale (par exemple, 16).
// Processus :
// À chaque étape, on examine toutes les boîtes actives.
// Pour chaque boîte, on simule toutes les divisions possibles le long de l'axe de l'index Thomson (c'est une
// simplification par rapport au Wu original qui teste R, G, B). Pour chaque division simulée, on calcule la réduction
// de variance que cette division entraînerait. On sélectionne la boîte et le point de division qui offrent la plus
// grande réduction de variance globale. Cette boîte est alors réellement divisée en deux sous-boîtes. Les deux
// nouvelles sous-boîtes remplacent l'ancienne boîte dans la liste des boîtes actives, et le nombre total de boîtes
// augmente de un. Critère d'arrêt : Le processus de division continue jusqu'à ce que le nombre de boîtes atteigne
// PALETTE_SIZE (ou qu'aucune division ne puisse réduire significativement la variance).

// 4. Extraction de la Palette Finale :
// Centroïdes de Boîte : Pour chacune des PALETTE_SIZE boîtes finales, on calcule la couleur moyenne (le "centroïde") à
// partir des sommes R, G, B et du pixel_count calculés précédemment. Projection sur la Palette Thomson : Comme nous
// voulons des couleurs Thomson exactes dans la palette finale, on trouve la couleur Thomson la plus proche de chaque
// centroïde calculé. Gestion de l'Unicité et Forçage : Pour garantir l'unicité des couleurs dans la palette finale, on
// garde une trace des couleurs Thomson déjà sélectionnées. Point important : On force l'inclusion du noir et du blanc
// (en trouvant leurs plus proches équivalents Thomson) au début de cette étape. Les slots restants sont remplis par les
// couleurs issues de l'algorithme de Wu. Si le Wu devait générer le noir ou le blanc, il serait simplement ignoré car
// déjà inclus. Si, après le forçage et l'ajout des couleurs de Wu, la palette n'est pas pleine, les slots restants sont
// remplis avec des couleurs Thomson uniques choisies aléatoirement (cas rare).



// --- Fonction principale generate_palette_wu_thomson_aware ---
void generate_palette_wu_thomson_aware(uint8_t *framed_image, int width, int height,
									   Color thomson_palette_source[NUM_THOMSON_COLORS],
									   Color generated_palette[PALETTE_SIZE])
{
	printf("--- Generating palette using Wu Thomson-Aware ---\n");

	memset(thomson_histogram, 0, sizeof(thomson_histogram));

	printf("  Building Thomson-aware histogram...\n");
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pixel_base_index = (y * width + x) * 3;
			uint8_t r = framed_image[pixel_base_index];
			uint8_t g = framed_image[pixel_base_index + 1];
			uint8_t b = framed_image[pixel_base_index + 2];

			int thomson_idx = find_closest_thomson_idx(r, g, b, thomson_palette_source, NULL);
			if (thomson_idx != -1) {
				thomson_histogram[thomson_idx]++;
			}
		}
	}
	printf("  Histogram built.\n");

	WuBox *active_boxes = (WuBox *)malloc(sizeof(WuBox) * PALETTE_SIZE);
	if (!active_boxes) {
		fprintf(stderr, "Error: Memory allocation failed for active_boxes.\n");
		return;
	}
	active_boxes[0].min_idx = 0;
	active_boxes[0].max_idx = NUM_THOMSON_COLORS - 1;
	calculate_box_moments(&active_boxes[0], thomson_palette_source); // Appel corrigé

	int num_boxes = 1;

	printf("  Splitting boxes...\n");
	while (num_boxes < PALETTE_SIZE) {
		int best_box_to_split_idx = -1;
		double max_variance_reduction_overall = -1.0;

		for (int i = 0; i < num_boxes; i++) {
			WuBox temp_box_to_split = active_boxes[i];

			// Recalculer les moments pour la boîte temporaire, en passant thomson_palette_source
			calculate_box_moments(&temp_box_to_split, thomson_palette_source);
			double original_variance = temp_box_to_split.variance;

			int best_local_split_idx = -1;
			double max_local_variance_reduction = -1.0;

			for (int split_idx = temp_box_to_split.min_idx; split_idx < temp_box_to_split.max_idx; split_idx++) {
				WuBox box1, box2;
				box1.min_idx = temp_box_to_split.min_idx;
				box1.max_idx = split_idx;
				// Passer thomson_palette_source
				calculate_box_moments(&box1, thomson_palette_source);
				box2.min_idx = split_idx + 1;
				box2.max_idx = temp_box_to_split.max_idx;
				// Passer thomson_palette_source
				calculate_box_moments(&box2, thomson_palette_source);

				double current_reduction = original_variance - (box1.variance + box2.variance);
				if (current_reduction > max_local_variance_reduction) {
					max_local_variance_reduction = current_reduction;
					best_local_split_idx = split_idx;
				}
			}

			if (max_local_variance_reduction > max_variance_reduction_overall) {
				max_variance_reduction_overall = max_local_variance_reduction;
				best_box_to_split_idx = i;
			}
		}

		if (best_box_to_split_idx != -1 && max_variance_reduction_overall > 0) {
			WuBox *box_to_split_ptr = &active_boxes[best_box_to_split_idx];
			WuBox *new_box_ptr = &active_boxes[num_boxes];

			double current_max_local_reduction = -1.0;
			int actual_split_idx = -1;

			for (int split_idx = box_to_split_ptr->min_idx; split_idx < box_to_split_ptr->max_idx; split_idx++) {
				WuBox box1, box2;
				box1.min_idx = box_to_split_ptr->min_idx;
				box1.max_idx = split_idx;
				// Passer thomson_palette_source
				calculate_box_moments(&box1, thomson_palette_source);
				box2.min_idx = split_idx + 1;
				box2.max_idx = box_to_split_ptr->max_idx;
				// Passer thomson_palette_source
				calculate_box_moments(&box2, thomson_palette_source);

				double current_reduction = box_to_split_ptr->variance - (box1.variance + box2.variance);
				if (current_reduction > current_max_local_reduction) {
					current_max_local_reduction = current_reduction;
					actual_split_idx = split_idx;
				}
			}

			if (actual_split_idx != -1) {
				new_box_ptr->min_idx = actual_split_idx + 1;
				new_box_ptr->max_idx = box_to_split_ptr->max_idx;
				calculate_box_moments(new_box_ptr, thomson_palette_source); // Appel corrigé

				box_to_split_ptr->max_idx = actual_split_idx;
				calculate_box_moments(box_to_split_ptr, thomson_palette_source); // Appel corrigé
				num_boxes++;
				printf("    Split box %d. Total boxes: %d\n", best_box_to_split_idx, num_boxes);
			} else {
				printf("    Could not find a valid split for box %d. Breaking.\n", best_box_to_split_idx);
				break;
			}

		} else {
			printf("  No more significant variance reduction possible. Breaking.\n");
			break;
		}
	}
	printf("  Finished splitting boxes. Total boxes created: %d\n", num_boxes);

	// --- CONSTRUCTION DE LA PALETTE FINALE AVEC FORÇAGE N&B ---
	memset(is_thomson_color_used_in_generated_palette, false, sizeof(bool) * NUM_THOMSON_COLORS);
	int final_palette_count = 0;

	// 1. Forcer l'inclusion du noir
	//int black_thomson_idx = find_closest_thomson_idx(0, 0, 0, thomson_palette_source, NULL);
	//if (black_thomson_idx != -1) {
	//	generated_palette[final_palette_count] = thomson_palette_source[black_thomson_idx];
	//	is_thomson_color_used_in_generated_palette[black_thomson_idx] = true;
	//	printf("  Forced palette[%d]: Black (Thomson Idx:%d, R:%d G:%d B:%d)\n", final_palette_count, black_thomson_idx,
	//		   generated_palette[final_palette_count].r, generated_palette[final_palette_count].g,
	//		   generated_palette[final_palette_count].b);
	//	final_palette_count++;
	//} else {
	//	fprintf(stderr, "Warning: Black (0,0,0) not found in Thomson palette, cannot force it.\n");
	//}

	//// 2. Forcer l'inclusion du blanc (seulement si ce n'est pas déjà le noir)
	//int white_thomson_idx = find_closest_thomson_idx(255, 255, 255, thomson_palette_source, NULL);
	//if (white_thomson_idx != -1 && !is_thomson_color_used_in_generated_palette[white_thomson_idx]) {
	//	generated_palette[final_palette_count] = thomson_palette_source[white_thomson_idx];
	//	is_thomson_color_used_in_generated_palette[white_thomson_idx] = true;
	//	printf("  Forced palette[%d]: White (Thomson Idx:%d, R:%d G:%d B:%d)\n", final_palette_count, white_thomson_idx,
	//		   generated_palette[final_palette_count].r, generated_palette[final_palette_count].g,
	//		   generated_palette[final_palette_count].b);
	//	final_palette_count++;
	//} else if (white_thomson_idx == -1) {
	//	fprintf(stderr, "Warning: White (255,255,255) not found in Thomson palette, cannot force it.\n");
	//} else if (is_thomson_color_used_in_generated_palette[white_thomson_idx]) {
	//	printf("  White (Thomson Idx:%d) already included (might be the same as black).\n", white_thomson_idx);
	//}

	// 3. Ajouter les centroïdes Wu restants, en évitant les doublons
	for (int i = 0; i < num_boxes && final_palette_count < PALETTE_SIZE; i++) {
		if (active_boxes[i].pixel_count == 0) {
			continue;
		}
		Color centroid_color;
		centroid_color.r = (uint8_t)round((double)active_boxes[i].sum_r / active_boxes[i].pixel_count);
		centroid_color.g = (uint8_t)round((double)active_boxes[i].sum_g / active_boxes[i].pixel_count);
		centroid_color.b = (uint8_t)round((double)active_boxes[i].sum_b / active_boxes[i].pixel_count);

		int thomson_idx_for_centroid = find_closest_thomson_idx(centroid_color.r, centroid_color.g, centroid_color.b,
																thomson_palette_source, NULL);

		if (thomson_idx_for_centroid != -1 && !is_thomson_color_used_in_generated_palette[thomson_idx_for_centroid]) {
			generated_palette[final_palette_count] = thomson_palette_source[thomson_idx_for_centroid];
			is_thomson_color_used_in_generated_palette[thomson_idx_for_centroid] = true;
			printf("  Wu palette[%d]: Thomson Idx:%d (R:%d G:%d B:%d)\n", final_palette_count,
				   generated_palette[final_palette_count].thomson_idx, generated_palette[final_palette_count].r,
				   generated_palette[final_palette_count].g, generated_palette[final_palette_count].b);
			final_palette_count++;
		}
	}

	// 4. Remplir les slots restants
	while (final_palette_count < PALETTE_SIZE) {
		fprintf(stderr, "Warning: Palette not full after Wu and forcing. Filling with random unique Thomson colors.\n");
		int random_idx;
		do {
			random_idx = rand() % NUM_THOMSON_COLORS;
		} while (is_thomson_color_used_in_generated_palette[random_idx]);
		generated_palette[final_palette_count] = thomson_palette_source[random_idx];
		is_thomson_color_used_in_generated_palette[random_idx] = true;
		final_palette_count++;
	}

	free(active_boxes);
	printf("--- Wu Thomson-Aware Finished. Final palette size: %d ---\n\n", final_palette_count);
}













unsigned char clamp_color_component(double val)
{
	if (val < 0.0) return 0;
	if (val > 255.0) return 255;
	return (unsigned char)val;
}

void block_dithering_thomson_smart_propagation(const unsigned char *original_image, DitheredPixel *dithered_image,
											   int width, int height, int original_channels, Color pal[16], float *matrix)
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


				// propagation d'erreur sans matrice
				// if (current_x + 1 < width) {
				// 	int neighbor_float_idx = (y * width + (current_x + 1)) * 3;
				// 	image_float[neighbor_float_idx] += error_r * 7.0 / 16.0;
				// 	image_float[neighbor_float_idx + 1] += error_g * 7.0 / 16.0;
				// 	image_float[neighbor_float_idx + 2] += error_b * 7.0 / 16.0;
				// }
    //
				// // (x-1, y+1) - en bas à gauche (bloc voisin en dessous)
				// if (current_x - 1 >= 0 && y + 1 < height) {
				// 	int neighbor_float_idx = ((y + 1) * width + (current_x - 1)) * 3;
				// 	image_float[neighbor_float_idx] += error_r * 3.0 / 16.0;
				// 	image_float[neighbor_float_idx + 1] += error_g * 3.0 / 16.0;
				// 	image_float[neighbor_float_idx + 2] += error_b * 3.0 / 16.0;
				// }
    //
				// // (x, y+1) - en bas (bloc voisin en dessous)
				// if (y + 1 < height) {
				// 	int neighbor_float_idx = ((y + 1) * width + current_x) * 3;
				// 	image_float[neighbor_float_idx] += error_r * 5.0 / 16.0;
				// 	image_float[neighbor_float_idx + 1] += error_g * 5.0 / 16.0;
				// 	image_float[neighbor_float_idx + 2] += error_b * 5.0 / 16.0;
				// }
    //
				// // (x+1, y+1) - en bas à droite (bloc voisin en dessous)
				// if (current_x + 1 < width && y + 1 < height) {
				// 	int neighbor_float_idx = ((y + 1) * width + (current_x + 1)) * 3;
				// 	image_float[neighbor_float_idx] += error_r * 1.0 / 16.0;
				// 	image_float[neighbor_float_idx + 1] += error_g * 1.0 / 16.0;
				// 	image_float[neighbor_float_idx + 2] += error_b * 1.0 / 16.0;
				// }


				// propagation d'erreur avec matrix dynamique
				int matrix_size = matrix[0];
				for (int i = 0; i < matrix_size; i++) {
					int xm = matrix[i * 3 + 1];
					int ym = matrix[i * 3 + 2];
					float value = matrix[i * 3 + 3];
					// printf("x=%d y=%d f=%f\n", xm, ym, value);

					if ((current_x + xm < width) && (current_x + xm >= 0) && (y + ym < height) ) {
						int neighbor_float_idx = ((y + ym) * width + (current_x + xm)) * 3;
						image_float[neighbor_float_idx] += error_r * value;
						image_float[neighbor_float_idx + 1] += error_g * value;
						image_float[neighbor_float_idx + 2] += error_b * value;
					}
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
	int errors_count = 0;

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
				errors_count++;
				printf("ERREUR DÉTECTÉE: Bloc à (%d, %d) contient %d couleurs uniques.\n", x_block_start, y,
					   color_count);
			}
		}
	}

	if (all_respected) {
		printf("RÉUSSITE : Toutes les contraintes de 2 couleurs par bloc sont respectées.\n");
	} else {
		printf("ATTENTION : %d blocs ne respectent PAS la contrainte de 2 couleurs. "
			   "Ceci est inattendu avec l'algorithme actuel et pourrait indiquer une erreur de logique.\n",
			   errors_count);
	}
	printf("-----------------------------------------------------------------\n");
	return all_respected;
}
