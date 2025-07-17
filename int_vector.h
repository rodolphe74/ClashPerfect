#ifndef  INT_VECTOR
#define INT_VECTOR

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
	uint8_t *data;
	size_t size;
	size_t capacity;
} IntVector;

void init_vector(IntVector *vec);
void push_back(IntVector *vec, uint8_t value);
void free_vector(IntVector *vec);

#endif // ! INT_VECTOR