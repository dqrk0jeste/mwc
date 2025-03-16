#include "mwc.h"
#include "config.h"

#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <fcft/fcft.h>
#include <pixman.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/interfaces/wlr_buffer.h>

extern struct mwc_server server;

struct pixman_buffer {
	struct wlr_buffer base;

  pixman_image_t *image;
};

static struct fcft_font *font = NULL;

static enum fcft_subpixel subpixel_mode = FCFT_SUBPIXEL_DEFAULT;

static void
render_glyphs(struct pixman_buffer *buffer, int32_t *x, pixman_image_t *color, size_t count,
              const struct fcft_glyph *glyphs[static count], long kern[static count]) {
  for(size_t i = 0; i < count; i++) {
    const struct fcft_glyph *g = glyphs[i];
    if(g == NULL) continue;

    *x += kern[i];

    pixman_image_composite32(PIXMAN_OP_OVER, color, g->pix, buffer->image, 0, 0, 0, 0,
                             *x + g->x, font->ascent - g->y, g->width, g->height);

    *x += g->advance.x;
  }
}

static void
render_chars(const char32_t *text, size_t len, struct pixman_buffer *buffer, pixman_image_t *color) {
  const struct fcft_glyph *glyphs[len];
  long kern[len];
  int width = 0;

  for(size_t i = 0; i < len; i++) {
    glyphs[i] = fcft_rasterize_char_utf32(font, text[i], subpixel_mode);
    if(glyphs[i] == NULL) continue;

    kern[i] = 0;
    if(i > 0) {
      fcft_kerning(font, text[i - 1], text[i], &kern[i], NULL);
    }

    width += kern[i] + glyphs[i]->advance.x;
  }

  int x = width;
  render_glyphs(buffer, &x, color, len, glyphs, kern);
}

char32_t *
convert_cstring_to_unicode(char *s) {
  /* todo: move this to some init thingy */
  setlocale(LC_ALL, "en_US.utf8");

  char32_t *unicode = calloc(strlen(s) + 1, sizeof(char32_t));
  mbstate_t ps = {0};
  char *in = s;
  char *end = s + strlen(s) + 1;

  size_t ret;
  size_t len = 0;
  while((ret = mbrtoc32(&unicode[len], in, end - in, &ps)) != 0) {
    if(ret > (size_t)(-3)) continue;

    in += ret;
    len++;
  }

  return unicode;
}

static void pixman_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
	struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	free(buffer);
}

static bool pixman_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                                         uint32_t flags, void **data,
                                                         uint32_t *format, size_t *stride) {
	struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	/**data = cairo_image_surface_get_data(buffer->surface);*/
	/**stride = cairo_image_surface_get_stride(buffer->surface);*/
	/**format = DRM_FORMAT_ARGB8888;*/

	return true;
}

static void pixman_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	/* this space is intentionally left blank */
}

static const struct wlr_buffer_impl pixman_buffer_impl = {
	.destroy = pixman_buffer_handle_destroy,
	.begin_data_ptr_access = pixman_buffer_handle_begin_data_ptr_access,
	.end_data_ptr_access = pixman_buffer_handle_end_data_ptr_access,
};

struct pixman_buffer *
text_buffer_create(char *s, uint32_t width, uint32_t height) {
  struct pixman_buffer *buffer = calloc(1, sizeof(*buffer));

  wlr_buffer_init(&buffer->base, &pixman_buffer_impl, 0, 0);

  pixman_region32_t clip;
  pixman_region32_init_rect(&clip, 0, 0, width, height);
  pixman_image_set_clip_region32(buffer->image, &clip);
  pixman_region32_fini(&clip);

  char32_t *unicode = convert_cstring_to_unicode(s);
  pixman_image_t *foreground_color = pixman_image_create_solid_fill(&server.config->titlebar_title_color);

  render_chars(unicode, strlen(s), buffer, foreground_color);

  pixman_image_unref(foreground_color);
  wlr_buffer_drop(&buffer->base);

  return buffer;
}

/*void*/
/*text_bu*/
