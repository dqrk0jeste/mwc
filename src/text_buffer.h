#include <fcft/fcft.h>
#include <stdint.h>
#include <pixman.h>
#include <wlr/types/wlr_buffer.h>

struct pixman_buffer {
	struct wlr_buffer base;

  pixman_image_t *image;
};

struct pixman_buffer *
text_buffer_create(char *s, uint32_t width, uint32_t height);

void
text_buffer_destroy(struct pixman_buffer *buffer);

void
text_buffer_adjust_to_size(struct pixman_buffer *buffer, uint32_t width, uint32_t height);
