#include <stdint.h>
#include <pixman.h>

struct pixman_buffer *
text_buffer_create(char *s, uint32_t width, uint32_t height);
