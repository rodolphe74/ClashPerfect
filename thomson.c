#include "thomson.h"
#include <float.h>
#include <math.h>

void init_thomson_palette(Color pal[4096])
{
	int index = 0;
	for (int b = 0; b < 16; b++) {
		for (int g = 0; g < 16; g++) {
			for (int r = 0; r < 16; r++) {
				uint8_t r8 = red_255[r].r;
				uint8_t g8 = green_255[g].g;
				uint8_t b8 = blue_255[b].b;
				//index = red_255[r].thomson_idx + green_255[g].thomson_idx + blue_255[b].thomson_idx;

				////printf("Color[%d]=%d,%d,%d\n", index, r8, g8, b8);

				//thomson_palette[index].r = r8;
				//thomson_palette[index].g = g8;
				//thomson_palette[index].b = b8;
				//thomson_palette[index].thomson_idx = 0;

				index = red_255[r].thomson_idx + green_255[g].thomson_idx + blue_255[b].thomson_idx;

				// printf("Color[%d]=%d,%d,%d\n", index, r8, g8, b8);

				pal[index].r = r8;
				pal[index].g = g8;
				pal[index].b = b8;
				pal[index].thomson_idx = 0;
				index++;
			}
		}
	}
	printf("");
}

void find_closest_thomson_palette(Color optimalPalette[PALETTE_SIZE], Color thomson_palette[NUM_THOMSON_COLORS],
								  Color newPalette[PALETTE_SIZE])
{
	float currentDistance = 0;
	for (int i = 0; i < PALETTE_SIZE; i++) {


		float minDistance = FLT_MAX;
		int minIndex = 0;
		for (int j = 0; j < 4096; j++) {
			currentDistance = sqrtf(powf(thomson_palette[j].r - optimalPalette[i].r, 2) +
									powf(thomson_palette[j].g - optimalPalette[i].g, 2) +
									powf(thomson_palette[j].b - optimalPalette[i].b, 2));
			if (currentDistance < minDistance) {
				minDistance = currentDistance;
				minIndex = j;
			}
		}
		printf("o (%d,%d,%d)\n", optimalPalette[i].r, optimalPalette[i].g, optimalPalette[i].b);
		printf("t %f -> %d (%d,%d,%d)\n", minDistance, minIndex, thomson_palette[minIndex].r,
			   thomson_palette[minIndex].g, thomson_palette[minIndex].b);
		newPalette[i].r = thomson_palette[minIndex].r;
		newPalette[i].g = thomson_palette[minIndex].g;
		newPalette[i].b = thomson_palette[minIndex].b;
	}
}

int find_thomson_palette_index(int r, int g, int b, Color thomson_palette[NUM_THOMSON_COLORS])
{
	for (int i = 0; i < NUM_THOMSON_COLORS; i++)
		if (r == thomson_palette[i].r && g == thomson_palette[i].g && b == thomson_palette[i].b) return i;

	return 0; // ?
}

int find_palette_index(int r, int g, int b, Color palette[PALETTE_SIZE])
{
	for (int i = 0; i < PALETTE_SIZE; i++)
		if (r == palette[i].r && g == palette[i].g && b == palette[i].b) return i;

	return 0; // ?
}

void transpose_data_map_40(int columns, int lines, IntVector *src, IntVector *target)
{
	uint8_t current;
	uint8_t zero = 0;

	// Le nombre de lignes doit être un multiple de 8
	// La hauteur est inscrite dans l'entêtte du map
	// sous la forme (map_40->lines - 1) / 8

	int padding = lines % 8;
	int add_line = 8 - padding;

	for (int x = 0; x < columns; x++) {
		for (int y = 0; y < lines; y++) {
			// current = src.at(y * columns + x);
			current = src->data[y * columns + x];
			push_back(target, current);
		}

		if (padding)
			for (int y = 0; y < add_line; y++) push_back(target, zero);

		add_line = 8 - padding;
	}
}

int read_ahead(const IntVector *buffer_list, int idx)
{
	uint8_t current;
	uint8_t compare_to;

	// compare_to = buffer_list.at(idx);
	compare_to = buffer_list->data[idx];
	int repeat = 0;

	for (int i = idx + 1; i < buffer_list->size; i++) {
		// current = buffer_list.at(i);
		current = buffer_list->data[i];
		if (compare_to != current || repeat > 253) break;
		repeat++;
	}
	return repeat;
}

void write_segment(IntVector *target, const IntVector *buffer_list, int i, uint8_t seg_size)
{
	uint8_t current;
	uint8_t header[2];

	header[0] = 0;
	header[1] = seg_size;

	push_back(target, header[0]);
	push_back(target, header[1]);

	for (int j = i - seg_size; j < i; j++) {
		// current = buffer_list.at(j);
		current = buffer_list->data[j];
		push_back(target, current);
	}
}

void compress(IntVector *target, IntVector *buffer_list, int enclose)
{
	// Traitement du buffer;
	int i = 0;
	int seg = 0;
	unsigned char current;

	while (i < buffer_list->size) {
		int repeat = read_ahead(buffer_list, i);

		if (repeat == 0) {
			i += 1;
			seg++;

			if (seg > 254) {
				write_segment(target, buffer_list, i, seg);
				seg = 0;
			}
		} else {
			if (seg > 0) write_segment(target, buffer_list, i, (unsigned char)seg);

			i += (repeat + 1);
			seg = 0;

			unsigned char rep_count;
			rep_count = repeat + 1;
			push_back(target, rep_count);
			// current = buffer_list.at(i - repeat - 1);
			current = buffer_list->data[i - repeat - 1];
			push_back(target, current);
		}
	}

	// flush
	if (seg > 0) write_segment(target, buffer_list, i, seg);

	// cloture ?
	if (enclose) {
		unsigned char cloture[2] = {0, 0};
		push_back(target, cloture[0]);
		push_back(target, cloture[1]);
	}
}

void clash_fragment_to_palette_indexed_bloc(const unsigned char *fragment, uint8_t *bloc, int blocSize, Color palette[PALETTE_SIZE])
{
	for (int i = 0; i < blocSize; i++) {
		Color c = {fragment[i * COLOR_COMP], fragment[i * COLOR_COMP + 1], fragment[i * COLOR_COMP + 2]};
		int idx = find_palette_index(c.r, c.g, c.b, palette);

		//if (idx != 0) {
		//	printf("");
		//}

		//printf("-->Color: %d,%d,%d = %d\n", c.r, c.g, c.b, idx);
		bloc[8 - 1 - i] = idx;
	}
}


int getIndexColorThomsonTo(int back_index, int fore_index)
{
	// Palette thomson TO xyBVRBVR | x = 0 : fd pastel | y = 0 fo pastel
	// N,R,V,J,B,M,C,BL (fonce)
	// x,x,x,x,x,x,x,OR (pastel)

	// couleur > 7 = pastel
	int subst_back = (back_index > 7 ? 8 : 0);
	int subst_fore = (fore_index > 7 ? 8 : 0);
	unsigned char idx = (back_index > 7 ? 0 : 1) << 7 | (fore_index > 7 ? 0 : 1) << 6 | (fore_index - subst_fore) << 3 |
						(back_index - subst_back);

	return idx;
}

int getIndexColorThomsonMo(int back_index, int fore_index)
{
	// Palette thomson MO5/6 xBVRyBVR | x = 1 : fd pastel | y = 1 fo pastel
	// N,R,V,J,B,M,C,BL (fonce)
	// x,x,x,x,x,x,x,OR (pastel)

	// couleur > 7 = pastel
	unsigned char idx =
		(fore_index > 7 ? 1 : 0) << 7 | (fore_index) << 4 | (back_index > 7 ? 1 : 0) << 3 | (back_index);

	return idx;
}

void thomson_encode_bloc(uint8_t bloc[8], uint8_t thomson_bloc[3])
{
	// Conversion du bloc en valeur thomson to/mo
	// en sortie :
	// thomson_bloc[0] = forme
	// thomson_bloc[1] = couleurs format TO
	// thomson_bloc[2] = couleurs format MO
	// En basic, le format de la couleur est spécifié en fonction de la config TO/MO
	// En SNAP-TO, le format de la couleur est toujours TO

	// recherche des couleurs
	int fd = bloc[0];
	int fo = -1;
	int val = 0 /*, coul = 0*/;

	for (int i = 0; i < 8; i++)
		if (bloc[i] != fd) fo = bloc[i];

	// Calcul forme
	for (int i = 7; i >= 0; i--)
		if (bloc[i] == fo) val += pow(2, i);

	// Couleur MO / TO
	thomson_bloc[1] = getIndexColorThomsonTo(fd, fo <= 0 ? 0 : fo);
	thomson_bloc[2] = getIndexColorThomsonMo(fd, fo <= 0 ? 0 : fo);

	thomson_bloc[0] = val;
}

void save_map_40_col(const char *filename, MAP_SEG *map_40, Color thomson_palette[NUM_THOMSON_COLORS], Color palette[PALETTE_SIZE])
{
	IntVector buffer_list, target_buffer_list;
	unsigned char current;

	FILE *fout;
	char map_filename[256];

	init_vector(&buffer_list);
	init_vector(&target_buffer_list);

	sprintf(map_filename, "%s.map", filename);
	if ((fout = fopen(map_filename, "wb")) == NULL) {
		fprintf(stderr, "Impossible d'ouvrir le fichier données en écriture\n");
		return;
	}

	transpose_data_map_40(map_40->columns, map_40->lines, &map_40->rama, &buffer_list);
	compress(&target_buffer_list, &buffer_list, 1);

	init_vector(&buffer_list);

	transpose_data_map_40(map_40->columns, map_40->lines, &map_40->ramb, &buffer_list);
	compress(&target_buffer_list, &buffer_list, 1);

	// Ecriture de l'entete
	uint16_t size = (uint16_t)target_buffer_list.size + 3 + 39;

	if (size % 2 == 1) {
		// Apparement, la taille doit être paire
		unsigned char zero = 0;
		push_back(&target_buffer_list, zero);
		size++;
	}

	unsigned char header[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	header[2] = size & 255;
	header[1] = (size >> 8) & 255;

	header[6] = map_40->columns - 1;
	header[7] = (map_40->lines - 1) / 8; // Le fichier map ne fonctionne que sur multiple de 8

	fwrite(header, sizeof(uint8_t), 8, fout);

	// Ecriture du buffer map compressé dans le fichier de sortie
	// cout << "ToSnap buffer size:" << target_buffer_list.size() << endl;
	for (int i = 0; i < target_buffer_list.size; i++) {
		// current = target_buffer_list.at(i);
		current = target_buffer_list.data[i];
		fwrite(&current, sizeof(uint8_t), 1, fout);
	}

	// Ecriture footer TO-SNAP
	uint8_t to_snap[40];

	memset(to_snap, 0, 39);
	to_snap[0] = 0; // 16 couleurs 40 colonnes
	to_snap[2] = 0; // tour de l'écran
	to_snap[4] = 0; // mode 3 console

	for (int i = 0; i < 16; i++) {
		uint16_t thomson_palette_value = find_thomson_palette_index(palette[i].r, palette[i].g, palette[i].b, thomson_palette);
		printf(" (%d,%d,%d)  thomson[%d]=%d\n", palette[i].r, palette[i].g, palette[i].b, i,
			   thomson_palette_value);
		to_snap[5 + i * 2] = (thomson_palette_value >> 8) & 255;
		to_snap[5 + i * 2 + 1] = thomson_palette_value & 255;
	}

	to_snap[37] = 0xA5;
	to_snap[38] = 0x5A;
	fwrite(to_snap, sizeof(unsigned char), 39, fout);

	// Ecriture du footer
	unsigned char footer[] = {0, 0, 0, 0, 0};

	footer[0] = 255;
	fwrite(footer, sizeof(unsigned char), 5, fout);

	fflush(fout);
	fclose(fout);

	printf("TO-SNAP créé\n");

	free_vector(&buffer_list);
	free_vector(&target_buffer_list);

	// Ecriture du chargeur TO-SNAP
	// char fname_snap_out[256];
	//
	// sprintf(fname_snap_out, "%s.bld", filename);
	// FILE *tosnap_out = fopen(fname_snap_out, "w");
	//
	// fprintf(tosnap_out, "10 DIM T%%(10000)\n");
	// fprintf(tosnap_out, "20 DEFFNC(R)=MAX(-R-1,R)\n");
	// fprintf(tosnap_out, "30 LOADP \"%s\",T%%(10000)\n", map_filename);
	// fprintf(tosnap_out, "40 T=T%%(10000)\n");
	// fprintf(tosnap_out, "50 T=T+1 : IF T%%(T)<>-23206 THEN END\n");
	// fprintf(tosnap_out, "60 FOR I=15 TO 0 STEP -1:T=T+1:PALETTE I,FNC(T%%(T)):NEXT\n");
	// fprintf(tosnap_out, "70 T=T+1 : CONSOLE,,,,T%%(T)\n");
	// fprintf(tosnap_out, "80 T=T+1 : SCREEN,,T%%(T)\n");
	// fprintf(tosnap_out, "90 T=T+1 : POKE &H605F,T%%(T)\n");
	// fprintf(tosnap_out, "100 PUT(0,0),T%%(10000)\n");
	// fflush(tosnap_out);
	// fclose(tosnap_out);
	//
	// fflush(stdout);
}

void save_as_to_snap(const char *name, const uint8_t *output_image_data, Color thomson_palette[NUM_THOMSON_COLORS], Color palette[16])
{
	MAP_SEG map_40;
	init_vector(&map_40.rama);
	init_vector(&map_40.ramb);
	unsigned char *clash_fragment = malloc(8 * COLOR_COMP);
	if (!clash_fragment) return;
	uint8_t current_bloc[8];
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x += 8) {
			int length = x + 8 > WIDTH ? WIDTH - x : 8;
			memset(clash_fragment, 0, 8 * COLOR_COMP);
			for (int i = 0; i < length; i++) {
				int output_pixel_idx = (y * WIDTH + x + i) * COLOR_COMP;
				uint8_t r = output_image_data[output_pixel_idx];
				uint8_t g = output_image_data[output_pixel_idx + 1];
				uint8_t b = output_image_data[output_pixel_idx + 2];
				clash_fragment[i * COLOR_COMP] = r;
				clash_fragment[i * COLOR_COMP + 1] = g;
				clash_fragment[i * COLOR_COMP + 2] = b;
			}
			clash_fragment_to_palette_indexed_bloc(clash_fragment, current_bloc, 8, palette);
			uint8_t ret[3];
			thomson_encode_bloc(current_bloc, ret);
			push_back(&map_40.rama, ret[0]);
			push_back(&map_40.ramb, ret[1]);
		}
	}
	map_40.lines = HEIGHT;
	map_40.columns = WIDTH / 8 + (WIDTH % 8 == 0 ? 0 : 1);
	save_map_40_col(name, &map_40, thomson_palette, palette);
}
