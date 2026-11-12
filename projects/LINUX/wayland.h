#ifndef WAYLAND_H
#define WAYLAND_H

//
// native wayland (wlr-layer-shell) overlay backend.
// implemented in wayland.cpp
//

#ifndef BOOL
typedef int BOOL;
#endif

namespace wl {

BOOL init(void);
void shutdown(void);
void pump(void);
BOOL should_close(void);
void frame_begin(void);
void frame_end(void);
void draw_rect(float x, float y, float w, float h, unsigned char r,
               unsigned char g, unsigned char b);
void draw_fill_rect(float x, float y, float w, float h, unsigned char r,
                    unsigned char g, unsigned char b);

} // namespace wl

#endif // WAYLAND_H
