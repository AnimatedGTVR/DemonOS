#ifndef DEMON_TUI_H
#define DEMON_TUI_H

#include <stddef.h>

/* Plain-ASCII TUI primitives shared between MakoBox (kernel console),
   xterm (userspace, DemonX font is ASCII-only), and the disk installer.
   Deliberately '+'/'-'/'|' box drawing rather than CP437/Unicode line
   characters -- xterm's own font has no glyphs for those, and this way
   the exact same output looks right over serial, VGA text mode, and the
   graphical terminal. `emit` receives one already-built line at a time
   (no trailing newline); it's the same shape as shell_backend's
   emit_line in shell_commands.h, kept separate here since a plain
   function pointer is enough for this module's needs. */
typedef void (*tui_emit_fn)(void *context, const char *line);

/* "+----...----+" of the given width (including both '+' corners). */
void tui_box_edge(tui_emit_fn emit, void *context, size_t width);

/* "| text, left-padded with spaces to width-2 |". Text longer than
   width-2 is truncated. */
void tui_box_line(tui_emit_fn emit, void *context, size_t width, const char *text);

/* A centered title between two box edges, e.g.:
   +--------------+
   |   DEMONOS    |
   +--------------+ */
void tui_banner(tui_emit_fn emit, void *context, size_t width, const char *title);

/* "[###.......] NN% label" sized to fit exactly in `width` columns
   (bar length is derived from width and the label's length). percent is
   clamped to [0,100]. label may be NULL. */
void tui_progress_bar(tui_emit_fn emit, void *context, size_t width,
                      unsigned percent, const char *label);

#endif
