#ifndef DITHER_H
#define DITHER_H

#include "dither.h"
#include "thomson.h"
#include <stdbool.h>


// --- Global Constants and Definitions ---
// These would typically be in a header file (e.g., "palette.h") for a larger project.

#define NUM_THOMSON_COLORS 4096 // Assuming Thomson palette has 16*16*16 = 4096 colors
#define PALETTE_SIZE 16			// The target size of the generated palette (all derived from Wu)

// Constants for Wu's algorithm histogram (ADAPTED FOR THOMSON PALETTE GRANULARITY)
#define THOMSON_LEVELS_PER_DIM 16				   // Number of levels per R, G, B component (0-15) for Thomson
#define HIST_SIZE_DIM (THOMSON_LEVELS_PER_DIM + 1) // Dimension of the 3D histogram array (0-16 for indices)
#define MAX_COLOR_COMPONENT_VALUE 256			   // Max value for a color component (0-255)

// --- Data Structures ---
// These typedefs should also be in a header file if used across multiple .c files.

typedef struct {
	unsigned char r, g, b;
} Pixel; // Used for reading raw image data (e.g., from an image file)

// Structure for intermediate "ideal" colors calculated by Wu's algorithm
typedef struct {
	double r_val, g_val, b_val; // Floating-point RGB values for ideal color
} WuIdealColor;

// Structure for color cubes used in Wu's algorithm
typedef struct {
	int r0, g0, b0; // Min R, G, B histogram indices
	int r1, g1, b1; // Max R, G, B histogram indices
} Cube;

// --- Global Static Moment Tables for Wu's Algorithm ---
// These tables must be reset to zero before processing each new image.
// Declared as static to limit their scope to this file.
static long vwt[HIST_SIZE_DIM][HIST_SIZE_DIM][HIST_SIZE_DIM]; // Total pixel count (volume)
static long vmr[HIST_SIZE_DIM][HIST_SIZE_DIM][HIST_SIZE_DIM]; // Sum of red component values
static long vmg[HIST_SIZE_DIM][HIST_SIZE_DIM][HIST_SIZE_DIM]; // Sum of green component values
static long vmb[HIST_SIZE_DIM][HIST_SIZE_DIM][HIST_SIZE_DIM]; // Sum of blue component values
static double m2[HIST_SIZE_DIM][HIST_SIZE_DIM]
				[HIST_SIZE_DIM]; // Sum of squared component values (for variance calculation)


double color_distance_sq(Color c1, Color c2);
unsigned char clamp_color_component(double val);
void block_dithering_thomson_smart_propagation(const unsigned char *original_image, DitheredPixel *dithered_image,
											   int width, int height, int original_channels, Color pal[16]);
bool verify_color_clash(const DitheredPixel *dithered_image, int width, int height);
void find_rgb_palette(const unsigned char *original_image, Color pal[16]);


void generate_wu_only_palette(const unsigned char *image_data, int image_width, int image_height,
							  const Color thomson_palette[NUM_THOMSON_COLORS], Color generated_palette[PALETTE_SIZE]);

#endif // ! DITHER_H
