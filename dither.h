#ifndef DITHER_H
#define DITHER_H

#include "thomson.h"
#include <stdbool.h>


// --- Définitions globales et structures ---
#define NUM_THOMSON_COLORS 4096 // Nombre total de couleurs dans la palette Thomson
#define PALETTE_SIZE 16			// Nombre de couleurs souhaité dans la palette finale

//// Structure pour une couleur (RGB 0-255 avec index Thomson)
//typedef struct {
//	uint8_t r, g, b;
//	int thomson_idx; // L'index de cette couleur dans la thomson_palette (-1 si non applicable)
//} Color;

typedef struct {
	int min_idx, max_idx;	  // Plage d'indices Thomson que cette boîte couvre
	long sum_r, sum_g, sum_b; // Sommes des composantes RGB des couleurs Thomson dans cette boîte
	uint32_t pixel_count;	  // Nombre total de pixels (réels) associés aux couleurs Thomson dans cette boîte
	double variance;		  // Variance des couleurs dans cette boîte (utilisée pour le critère de division)
} WuBox;

// --- Déclaration de la palette Thomson globale et du tableau d'unicité ---
extern Color thomson_palette[NUM_THOMSON_COLORS];
static bool is_thomson_color_used_in_generated_palette[NUM_THOMSON_COLORS];




double distance_squared(unsigned char r1, unsigned char g1, unsigned char b1, unsigned char r2, unsigned char g2,
						unsigned char b2);
int find_closest_thomson_idx(unsigned char r, unsigned char g, unsigned char b,
							 const Color thomson_pal[NUM_THOMSON_COLORS], const bool *current_used_flags);
unsigned char clamp_color_component(double val);

void block_dithering_thomson_smart_propagation(const unsigned char *original_image, DitheredPixel *dithered_image,
											   int width, int height, int original_channels, Color pal[16], float *matrix);
bool verify_color_clash(const DitheredPixel *dithered_image, int width, int height);
void generate_palette_wu_thomson_aware(uint8_t *framed_image, int width, int height,
									   Color thomson_palette_source[NUM_THOMSON_COLORS],
									   Color generated_palette[PALETTE_SIZE]);
#endif // ! DITHER_H
