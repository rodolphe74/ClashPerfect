#include "int_vector.h"

void init_vector(IntVector *vec)
{
	vec->size = 0;
	vec->capacity = 4;
	vec->data = (uint8_t *)malloc(vec->capacity * sizeof(uint8_t));
}

void push_back(IntVector *vec, uint8_t value)
{
	if (vec->size >= vec->capacity) {
		vec->capacity *= 2;
		void *tmp = realloc(vec->data, vec->capacity * sizeof(int));
		if (tmp == NULL) {
			vec->capacity /= 2;
			return;
		}
		vec->data = tmp;
	}
	vec->data[vec->size++] = value;
}

void free_vector(IntVector *vec)
{
	free(vec->data);
	vec->data = NULL;
	vec->size = vec->capacity = 0;
}