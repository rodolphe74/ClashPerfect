#ifndef THOMSON_H
#define THOMSON_H

#include <stdint.h>
#include "int_vector.h"
#include "global.h"

typedef struct {
	unsigned char r, g, b;
	uint16_t thomson_idx;
} Color;

typedef struct {
	int palette_idx;
} DitheredPixel;

typedef struct {
	uint8_t columns;
	uint8_t lines;
	IntVector rama;
	IntVector ramb;
} MAP_SEG;

static Color mo5_palette[16] = {
	/*  0 – Noir        */ {0, 0, 0},
	/*  1 – Rouge       */ {255, 0, 0},
	/*  2 – Vert        */ {0, 255, 0},
	/*  3 – Jaune       */ {255, 255, 0},
	/*  4 – Bleu        */ {0, 0, 255},
	/*  5 – Magenta     */ {255, 0, 255},
	/*  6 – Cyan        */ {0, 255, 255},
	/*  7 – Blanc       */ {255, 255, 255},
	/*  8 – Gris        */ {193, 193, 193},
	/*  9 – Vieux rose  */ {219, 142, 142},
	/* 10 – Vert clair  */ {142, 218, 142},
	/* 11 – Sable       */ {219, 219, 142},
	/* 12 – Bleu ciel   */ {142, 142, 219},
	/* 13 – Magenta cl. */ {219, 142, 219},
	/* 14 – Cyan clair  */ {193, 249, 249},
	/* 15 – Orange      */ {226, 193, 0}};

static Color red_255[16] = {{0, 0, 0, 0},	 {96, 0, 0, 1},	  {122, 0, 0, 2},  {142, 0, 0, 3},
							{158, 0, 0, 4},	 {170, 0, 0, 5},  {183, 0, 0, 6},  {193, 0, 0, 7},
							{204, 0, 0, 8},	 {211, 0, 0, 9},  {219, 0, 0, 10}, {226, 0, 0, 11},
							{234, 0, 0, 12}, {242, 0, 0, 13}, {249, 0, 0, 14}, {255, 0, 0, 15}};

// Green : 0 16 32 48 64 80 96 112 128 144 160 176 192 208 224 240
static Color green_255[16] = {{0, 0, 0, 0},		{0, 96, 0, 16},	  {0, 122, 0, 32},	{0, 142, 0, 48},
							  {0, 158, 0, 64},	{0, 170, 0, 80},  {0, 183, 0, 96},	{0, 193, 0, 112},
							  {0, 204, 0, 128}, {0, 211, 0, 144}, {0, 219, 0, 160}, {0, 226, 0, 176},
							  {0, 234, 0, 192}, {0, 242, 0, 208}, {0, 249, 0, 224}, {0, 255, 0, 240}};

// Blue : 0 256 512 768 1024 1280 1536 1792 2048 2304 2560 2816 3072 3328 3584 3840
static Color blue_255[16] = {{0, 0, 0, 0},		{0, 0, 96, 256},   {0, 0, 122, 512},  {0, 0, 142, 768},
							 {0, 0, 158, 1024}, {0, 0, 170, 1280}, {0, 0, 183, 1536}, {0, 0, 193, 1792},
							 {0, 0, 204, 2048}, {0, 0, 211, 2304}, {0, 0, 219, 2560}, {0, 0, 226, 2816},
							 {0, 0, 234, 3072}, {0, 0, 242, 3328}, {0, 0, 249, 3584}, {0, 0, 255, 3840}};

static Color thomson_palette_init[4096];

void init_thomson_palette(Color pal[4096]);
void find_closest_thomson_palette(Color optimalPalette[PALETTE_SIZE], Color thomson_palette[NUM_THOMSON_COLORS],
								  Color newPalette[PALETTE_SIZE]);

// TO-SNAP
int getIndexColorThomsonTo(int back_index, int fore_index);
int getIndexColorThomsonMo(int back_index, int fore_index);
void clash_fragment_to_palette_indexed_bloc(const unsigned char *fragment, uint8_t *bloc, int blocSize,
											Color palette[PALETTE_SIZE]);
void thomson_encode_bloc(uint8_t bloc[8], uint8_t thomson_bloc[3]);
int find_thomson_palette_index(int r, int g, int b, Color thomson_palette[NUM_THOMSON_COLORS]);
int find_palette_index(int r, int g, int b, Color palette[PALETTE_SIZE]);
void transpose_data_map_40(int columns, int lines, IntVector *src, IntVector *target);
int read_ahead(const IntVector *buffer_list, int idx);
void write_segment(IntVector *target, const IntVector *buffer_list, int i, uint8_t seg_size);
void compress(IntVector *target, IntVector *buffer_list, int enclose);
void save_map_40_col(const char *filename, MAP_SEG *map_40, Color thomson_palette[NUM_THOMSON_COLORS],
					 Color palette[PALETTE_SIZE]);
void save_as_to_snap(const char *name, const uint8_t *output_image_data, Color thomson_palette[NUM_THOMSON_COLORS],
					 Color palette[16]);

#endif