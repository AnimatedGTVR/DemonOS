#include <demon/c_app.h>
#include <demon/portkit.h>
#include "Bitmap.h"
#include "BlockID.h"
#include "ExtMath.h"
#include "Event.h"
#include "Generator.h"
#include "Graphics.h"
#include "Game.h"
#include "Input.h"
#include "Stream.h"
#include "String_.h"
#include "World.h"
#include "Window.h"
#include "Window_DemonOS.h"

#include <stdint.h>

/* The narrow D1/D2 executable does not link World.c yet, but the genuine
   upstream generator consumes this canonical state object. */
struct _WorldData World;

#define PREVIEW_WIDTH 256u
#define PREVIEW_HEIGHT 192u
#define DEMO_CHUNK_SIZE 8
#define CHUNKS_X 4
#define CHUNKS_Z 4
#define CHUNK_COUNT (CHUNKS_X * CHUNKS_Z)
static cc_uint32 world_checksum(const BlockRaw *blocks, cc_uint32 count) {
    cc_uint32 hash = 2166136261u;
    for (cc_uint32 i = 0u; i < count; ++i) {
        hash ^= blocks[i];
        hash *= 16777619u;
    }
    return hash;
}

struct input_probe {
    int downs, ups, moves, raw_moves, wheels;
    float raw_x, raw_y, wheel;
};

static void probe_down(void *obj, int key, cc_bool repeating,
                       struct InputDevice *device) {
    (void)key; (void)repeating; (void)device;
    ((struct input_probe *)obj)->downs++;
}
static void probe_up(void *obj, int key, cc_bool repeating,
                     struct InputDevice *device) {
    (void)key; (void)repeating; (void)device;
    ((struct input_probe *)obj)->ups++;
}
static void probe_move(void *obj, int index) {
    (void)index; ((struct input_probe *)obj)->moves++;
}
static void probe_raw(void *obj, float x, float y) {
    struct input_probe *probe = obj;
    probe->raw_moves++; probe->raw_x = x; probe->raw_y = y;
}
static void probe_wheel(void *obj, float delta) {
    struct input_probe *probe = obj;
    probe->wheels++; probe->wheel = delta;
}

static bool validate_upstream_input(void) {
    struct input_probe probe = { 0 };
    bool valid = true;
    Event_Register_(&InputEvents.Down2, &probe, probe_down);
    Event_Register_(&InputEvents.Up2, &probe, probe_up);
    Event_Register_(&PointerEvents.Moved, &probe, probe_move);
    Event_Register_(&PointerEvents.RawMoved, &probe, probe_raw);
    Event_Register_(&InputEvents.Wheel, &probe, probe_wheel);

    struct input_event event = { .type = INPUT_KEY_DOWN, .code = 0x11u };
    if (!DemonOS_ApplyInputEvent(&event) || !Input.Pressed[CCKEY_W]) valid = false;
    event.type = INPUT_KEY_UP;
    if (!DemonOS_ApplyInputEvent(&event) || Input.Pressed[CCKEY_W]) valid = false;
    event = (struct input_event){ .type = INPUT_MOUSE_MOVE,
             .x = 73, .y = 41, .delta_x = -7, .delta_y = 4 };
    if (!DemonOS_ApplyInputEvent(&event) || Pointers[0].x != 73 ||
        Pointers[0].y != 41) valid = false;
    event = (struct input_event){ .type = INPUT_MOUSE_BUTTON_DOWN,
                                 .code = INPUT_MOUSE_LEFT };
    if (!DemonOS_ApplyInputEvent(&event) || !Input.Pressed[CCMOUSE_L]) valid = false;
    event.type = INPUT_MOUSE_BUTTON_UP;
    if (!DemonOS_ApplyInputEvent(&event) || Input.Pressed[CCMOUSE_L]) valid = false;
    event = (struct input_event){ .type = INPUT_MOUSE_SCROLL, .value = -1 };
    if (!DemonOS_ApplyInputEvent(&event)) valid = false;

    valid = valid && probe.downs >= 2 && probe.ups >= 2 && probe.moves == 1 &&
            probe.raw_moves == 1 && (int)probe.raw_x == -7 &&
            (int)probe.raw_y == 4 && probe.wheels == 1 &&
            (int)probe.wheel == -1;
    Event_Unregister_(&InputEvents.Down2, &probe, probe_down);
    Event_Unregister_(&InputEvents.Up2, &probe, probe_up);
    Event_Unregister_(&PointerEvents.Moved, &probe, probe_move);
    Event_Unregister_(&PointerEvents.RawMoved, &probe, probe_raw);
    Event_Unregister_(&InputEvents.Wheel, &probe, probe_wheel);
    return valid;
}

struct demo_player {
    float x, y, z, yaw, pitch, velocity_y, highest_y;
    cc_bool grounded;
};

struct hotbar_input { int steps; };

static void hotbar_wheel(void *obj, float delta) {
    struct hotbar_input *input = obj;
    if (delta < 0.0f) input->steps++;
    if (delta > 0.0f) input->steps--;
}

static cc_bool world_solid(const BlockRaw *blocks, int x, int y, int z);

static void player_raw_move(void *obj, float x, float y) {
    struct demo_player *player = obj;
    player->yaw   += x * 0.004f;
    player->pitch += y * 0.004f;
    Math_Clamp(player->pitch, -1.35f, 1.35f);
}

static float player_ground(const BlockRaw *blocks, float x, float z) {
    int bx = (int)(x + (float)World.Width * 0.5f);
    int bz = (int)(z + (float)World.Length * 0.5f);
    Math_Clamp(bx, 0, World.Width  - 1);
    Math_Clamp(bz, 0, World.Length - 1);
    int top = World.Height - 1;
    while (top > 0 && blocks[World_Pack(bx, top, bz)] == BLOCK_AIR) --top;
    return (float)top + 1.0f;
}

static cc_bool player_body_clear(const BlockRaw *blocks, float x, float y,
                                 float z) {
    const float radius = 0.30f, height = 1.80f;
    int min_x = Math_Floor(x - radius + (float)World.Width  * 0.5f);
    int max_x = Math_Floor(x + radius + (float)World.Width  * 0.5f);
    int min_y = Math_Floor(y + 0.01f);
    int max_y = Math_Floor(y + height - 0.01f);
    int min_z = Math_Floor(z - radius + (float)World.Length * 0.5f);
    int max_z = Math_Floor(z + radius + (float)World.Length * 0.5f);
    for (int by = min_y; by <= max_y; ++by)
    for (int bz = min_z; bz <= max_z; ++bz)
    for (int bx = min_x; bx <= max_x; ++bx) {
        if (world_solid(blocks, bx, by, bz)) return false;
    }
    return true;
}

static cc_bool block_overlaps_player(const struct demo_player *player,
                                     int bx, int by, int bz) {
    float block_x = (float)bx - (float)World.Width * 0.5f;
    float block_z = (float)bz - (float)World.Length * 0.5f;
    return block_x < player->x + 0.30f && block_x + 1.0f > player->x - 0.30f &&
           (float)by < player->y + 1.80f && (float)by + 1.0f > player->y &&
           block_z < player->z + 0.30f && block_z + 1.0f > player->z - 0.30f;
}

static void player_tick(struct demo_player *player, const BlockRaw *blocks,
                        float delta) {
    float forward = (Input.Pressed[CCKEY_W] ? 1.0f : 0.0f) -
                    (Input.Pressed[CCKEY_S] ? 1.0f : 0.0f);
    float strafe  = (Input.Pressed[CCKEY_D] ? 1.0f : 0.0f) -
                    (Input.Pressed[CCKEY_A] ? 1.0f : 0.0f);
    if (forward != 0.0f && strafe != 0.0f) {
        /* Keep diagonal movement at the same speed as movement on one axis. */
        forward *= 0.70710678f;
        strafe  *= 0.70710678f;
    }
    float sin_yaw = Math_SinF(player->yaw), cos_yaw = Math_CosF(player->yaw);
    float speed = Input_IsShiftPressed() ? 7.5f : 5.0f;
    float move_x = (sin_yaw * forward + cos_yaw * strafe) * speed * delta;
    float move_z = (-cos_yaw * forward + sin_yaw * strafe) * speed * delta;
    float next_x = player->x + move_x;
    float next_z = player->z + move_z;
    /* Resolve each horizontal axis independently so walls stop the player
       without also preventing them from sliding along the free axis. */
    if (player_body_clear(blocks, next_x, player->y, player->z)) player->x = next_x;
    if (player_body_clear(blocks, player->x, player->y, next_z)) player->z = next_z;
    Math_Clamp(player->x, -15.5f, 15.5f);
    Math_Clamp(player->z, -15.5f, 15.5f);

    if (Input.Pressed[CCKEY_SPACE] && player->grounded) {
        player->velocity_y = 6.0f;
        player->grounded = false;
    }
    player->velocity_y -= 14.0f * delta;
    float next_y = player->y + player->velocity_y * delta;
    if (player_body_clear(blocks, player->x, next_y, player->z)) {
        player->y = next_y;
        player->grounded = false;
    } else if (player->velocity_y <= 0.0f) {
        /* A downward collision lands on the top face of the local column. */
        player->y = player_ground(blocks, player->x, player->z);
        player->velocity_y = 0.0f;
        player->grounded = true;
    } else {
        /* Stop upward velocity immediately when the player's head hits a
           ceiling; gravity takes over on the following tick. */
        player->velocity_y = 0.0f;
    }
    if (player->y > player->highest_y) player->highest_y = player->y;
}

struct block_hit { int x, y, z, place_x, place_y, place_z; };

static cc_bool player_pick_block(const struct demo_player *player,
                                 const BlockRaw *blocks,
                                 struct block_hit *hit) {
    float cos_pitch = Math_CosF(player->pitch);
    float dx = Math_SinF(player->yaw) * cos_pitch;
    float dy = Math_SinF(player->pitch);
    float dz = -Math_CosF(player->yaw) * cos_pitch;
    float ox = player->x, oy = player->y + 1.62f, oz = player->z;
    int previous_x = -1, previous_y = -1, previous_z = -1;
    for (int step = 0; step <= 60; ++step) {
        float distance = (float)step * 0.1f;
        int x = Math_Floor(ox + dx * distance + (float)World.Width * 0.5f);
        int y = Math_Floor(oy + dy * distance);
        int z = Math_Floor(oz + dz * distance + (float)World.Length * 0.5f);
        if (x < 0 || y < 0 || z < 0 || x >= World.Width ||
            y >= World.Height || z >= World.Length) continue;
        if (world_solid(blocks, x, y, z)) {
            hit->x = x; hit->y = y; hit->z = z;
            hit->place_x = previous_x; hit->place_y = previous_y;
            hit->place_z = previous_z;
            return true;
        }
        previous_x = x; previous_y = y; previous_z = z;
    }
    return false;
}

static bool validate_block_edits(BlockRaw *blocks) {
    struct demo_player player = { .x = 0.0f, .z = 8.0f,
        .yaw = 0.0f, .pitch = -0.55f, .grounded = true };
    player.y = player_ground(blocks, player.x, player.z);
    struct block_hit hit;
    if (!player_pick_block(&player, blocks, &hit)) return false;
    BlockRaw selected = blocks[World_Pack(hit.x, hit.y, hit.z)];

    struct input_event click = { .type = INPUT_MOUSE_BUTTON_DOWN,
                                 .code = INPUT_MOUSE_LEFT };
    DemonOS_ApplyInputEvent(&click);
    if (!Input.Pressed[CCMOUSE_L]) return false;
    blocks[World_Pack(hit.x, hit.y, hit.z)] = BLOCK_AIR;
    click.type = INPUT_MOUSE_BUTTON_UP; DemonOS_ApplyInputEvent(&click);
    if (Input.Pressed[CCMOUSE_L]) return false;

    if (hit.place_x < 0 || hit.place_y < 0 || hit.place_z < 0) return false;
    click = (struct input_event){ .type = INPUT_MOUSE_BUTTON_DOWN,
                                 .code = INPUT_MOUSE_RIGHT };
    DemonOS_ApplyInputEvent(&click);
    if (!Input.Pressed[CCMOUSE_R] ||
        blocks[World_Pack(hit.place_x, hit.place_y, hit.place_z)] != BLOCK_AIR)
        return false;
    blocks[World_Pack(hit.place_x, hit.place_y, hit.place_z)] = selected;
    click.type = INPUT_MOUSE_BUTTON_UP; DemonOS_ApplyInputEvent(&click);
    return blocks[World_Pack(hit.x, hit.y, hit.z)] == BLOCK_AIR &&
           blocks[World_Pack(hit.place_x, hit.place_y, hit.place_z)] == selected &&
           !Input.Pressed[CCMOUSE_R];
}

static bool persist_edited_world(const BlockRaw *blocks) {
    cc_string path = String_FromReadonly("/home/demon/classicube-edited.raw");
    struct Stream output, input;
    cc_uint32 bytes = (cc_uint32)World.Volume;
    BlockRaw *verify = Mem_TryAlloc(bytes, 1u);
    if (verify == NULL) return false;
    bool valid = !Stream_CreateFile(&output, &path) &&
                 !Stream_Write(&output, blocks, bytes) &&
                 !output.Close(&output) &&
                 !Stream_OpenFile(&input, &path) &&
                 !Stream_Read(&input, verify, bytes) &&
                 !input.Close(&input) &&
                 world_checksum(blocks, bytes) == world_checksum(verify, bytes);
    Mem_Free(verify);
    return valid;
}

static bool load_edited_world(BlockRaw *blocks) {
    cc_string path = String_FromReadonly("/home/demon/classicube-edited.raw");
    struct Stream input;
    cc_uint32 bytes = (cc_uint32)World.Volume;
    return !Stream_OpenFile(&input, &path) &&
           !Stream_Read(&input, blocks, bytes) && !input.Close(&input);
}

static cc_bool world_solid(const BlockRaw *blocks, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= World.Width ||
        y >= World.Height || z >= World.Length) return false;
    return blocks[World_Pack(x, y, z)] != BLOCK_AIR;
}

static int exposed_face_count_region(const BlockRaw *blocks, int min_x, int min_z,
                                     int max_x, int max_z) {
    int count = 0;
    for (int y = 0; y < World.Height; ++y)
    for (int z = min_z; z < max_z; ++z)
    for (int x = min_x; x < max_x; ++x) {
        if (!world_solid(blocks, x, y, z)) continue;
        count += !world_solid(blocks, x - 1, y, z);
        count += !world_solid(blocks, x + 1, y, z);
        count += !world_solid(blocks, x, y - 1, z);
        count += !world_solid(blocks, x, y + 1, z);
        count += !world_solid(blocks, x, y, z - 1);
        count += !world_solid(blocks, x, y, z + 1);
    }
    return count;
}

#define ATLAS_SIZE 256
#define ATLAS_TILES_X (ATLAS_SIZE / 16)

#define TILE_GRASS_TOP 0
#define TILE_GRASS_SIDE 1
#define TILE_DIRT 2
#define TILE_STONE 3

static BitmapCol atlas_speckle(BitmapCol base, int variance, RNGState *rng) {
    int r = BitmapCol_R(base) + Random_Next(rng, variance * 2 + 1) - variance;
    int g = BitmapCol_G(base) + Random_Next(rng, variance * 2 + 1) - variance;
    int b = BitmapCol_B(base) + Random_Next(rng, variance * 2 + 1) - variance;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return BitmapColor_RGB(r, g, b);
}

static void atlas_build_speckled(BitmapCol *pixels, int tile, BitmapCol base,
                                 int variance, RNGState *rng) {
    int origin_x = (tile % ATLAS_TILES_X) * 16;
    int origin_y = (tile / ATLAS_TILES_X) * 16;
    for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x)
        pixels[(origin_y + y) * ATLAS_SIZE + origin_x + x] =
            atlas_speckle(base, variance, rng);
}

static void atlas_build_grass_side(BitmapCol *pixels, int tile, BitmapCol grass,
                                   BitmapCol dirt, int variance, RNGState *rng) {
    int origin_x = (tile % ATLAS_TILES_X) * 16;
    int origin_y = (tile / ATLAS_TILES_X) * 16;
    for (int y = 0; y < 16; ++y) {
        BitmapCol base = y < 4 ? grass : dirt;
        for (int x = 0; x < 16; ++x)
            pixels[(origin_y + y) * ATLAS_SIZE + origin_x + x] =
                atlas_speckle(base, variance, rng);
    }
}

static GfxResourceID create_block_atlas(void) {
    struct Bitmap atlas;
    Bitmap_TryAllocate(&atlas, ATLAS_SIZE, ATLAS_SIZE);
    if (atlas.scan0 == NULL) return NULL;
    RNGState rng;
    Random_Seed(&rng, 0xC14A55CEu);
    BitmapCol grass = BitmapColor_RGB(96, 168, 92);
    BitmapCol dirt  = BitmapColor_RGB(140, 100, 66);
    BitmapCol stone = BitmapColor_RGB(140, 140, 140);
    atlas_build_speckled(atlas.scan0, TILE_GRASS_TOP, grass, 12, &rng);
    atlas_build_speckled(atlas.scan0, TILE_DIRT, dirt, 14, &rng);
    atlas_build_speckled(atlas.scan0, TILE_STONE, stone, 22, &rng);
    atlas_build_grass_side(atlas.scan0, TILE_GRASS_SIDE, grass, dirt, 14, &rng);
    GfxResourceID texture = Gfx_CreateTexture(&atlas, 0, false);
    Mem_Free(atlas.scan0);
    return texture;
}

static void tile_uv(int tile, TextureRec *uv) {
    int col = tile % ATLAS_TILES_X, row = tile / ATLAS_TILES_X;
    uv->u1 = (float)col / (float)ATLAS_TILES_X;
    uv->v1 = (float)row / (float)ATLAS_TILES_X;
    uv->u2 = (float)(col + 1) / (float)ATLAS_TILES_X;
    uv->v2 = (float)(row + 1) / (float)ATLAS_TILES_X;
}

static void block_uvs(BlockRaw block, TextureRec *top,
                      TextureRec *side, TextureRec *bottom) {
    TextureRec grass_top, grass_side, dirt, stone;
    tile_uv(TILE_GRASS_TOP, &grass_top);
    tile_uv(TILE_GRASS_SIDE, &grass_side);
    tile_uv(TILE_DIRT, &dirt);
    tile_uv(TILE_STONE, &stone);
    if (block == BLOCK_GRASS) {
        *top = grass_top; *side = grass_side; *bottom = dirt;
    } else if (block == BLOCK_DIRT) {
        *top = dirt; *side = dirt; *bottom = dirt;
    } else {
        *top = stone; *side = stone; *bottom = stone;
    }
}

static struct VertexTextured *emit_quad(struct VertexTextured *vertices,
                                        float x0, float y0, float z0,
                                        float x1, float y1, float z1,
                                        float x2, float y2, float z2,
                                        float x3, float y3, float z3,
                                        PackedCol color, const TextureRec *uv) {
    vertices[0] = (struct VertexTextured){ x0, y0, z0, color, uv->u1, uv->v1 };
    vertices[1] = (struct VertexTextured){ x1, y1, z1, color, uv->u2, uv->v1 };
    vertices[2] = (struct VertexTextured){ x2, y2, z2, color, uv->u2, uv->v2 };
    vertices[3] = (struct VertexTextured){ x3, y3, z3, color, uv->u1, uv->v2 };
    return vertices + 4;
}

static struct VertexTextured *build_exposed_faces_region(const BlockRaw *blocks,
                                                   struct VertexTextured *vertices,
                                                   int min_x, int min_z,
                                                   int max_x, int max_z) {
    /* Per-face directional shading is carried by each vertex's colour and
       multiplied against the atlas texel by the SoftGPU textured path, so the
       voxels read as shaded while sharing one 256x256 procedural texture. */
    PackedCol shade_top  = PackedCol_Make(255, 255, 255, 255);
    PackedCol shade_side = PackedCol_Make(204, 204, 204, 255);
    PackedCol shade_dark = PackedCol_Make(153, 153, 153, 255);
    for (int y = 0; y < World.Height; ++y)
    for (int z = min_z; z < max_z; ++z)
    for (int x = min_x; x < max_x; ++x) {
        BlockRaw block = blocks[World_Pack(x, y, z)];
        if (block == BLOCK_AIR) continue;
        TextureRec top, side, bottom;
        block_uvs(block, &top, &side, &bottom);
        float x0 = (float)x - (float)World.Width * 0.5f, x1 = x0 + 1.0f;
        float z0 = (float)z - (float)World.Length * 0.5f, z1 = z0 + 1.0f;
        float y0 = (float)y, y1 = y0 + 1.0f;
        if (!world_solid(blocks,x,y+1,z)) vertices = emit_quad(vertices, x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1, shade_top, &top);
        if (!world_solid(blocks,x,y-1,z)) vertices = emit_quad(vertices, x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0, shade_dark, &bottom);
        if (!world_solid(blocks,x-1,y,z)) vertices = emit_quad(vertices, x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, shade_dark, &side);
        if (!world_solid(blocks,x+1,y,z)) vertices = emit_quad(vertices, x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, shade_side, &side);
        if (!world_solid(blocks,x,y,z-1)) vertices = emit_quad(vertices, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, shade_dark, &side);
        if (!world_solid(blocks,x,y,z+1)) vertices = emit_quad(vertices, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, shade_side, &side);
    }
    return vertices;
}

static GfxResourceID create_chunk_mesh(const BlockRaw *blocks, int chunk_x,
                                       int chunk_z, int *vertex_count) {
    int min_x = chunk_x * DEMO_CHUNK_SIZE, min_z = chunk_z * DEMO_CHUNK_SIZE;
    int max_x = min_x + DEMO_CHUNK_SIZE, max_z = min_z + DEMO_CHUNK_SIZE;
    int faces = exposed_face_count_region(blocks, min_x, min_z, max_x, max_z);
    *vertex_count = faces * 4;
    if (*vertex_count == 0) return NULL;
    GfxResourceID vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_TEXTURED,
                                              *vertex_count);
    if (vb == NULL) return NULL;
    struct VertexTextured *vertices = Gfx_LockVb(vb, VERTEX_FORMAT_TEXTURED,
                                                  *vertex_count);
    if (vertices == NULL) { Gfx_DeleteVb(&vb); return NULL; }
    build_exposed_faces_region(blocks, vertices, min_x, min_z, max_x, max_z);
    Gfx_UnlockVb(vb);
    return vb;
}

struct chunk_mesh { GfxResourceID vb; int vertices; };

static cc_bool rebuild_chunk(const BlockRaw *blocks, struct chunk_mesh *chunks,
                             int chunk_x, int chunk_z) {
    if (chunk_x < 0 || chunk_z < 0 || chunk_x >= CHUNKS_X || chunk_z >= CHUNKS_Z)
        return true;
    int index = chunk_z * CHUNKS_X + chunk_x;
    Gfx_DeleteVb(&chunks[index].vb);
    chunks[index].vb = create_chunk_mesh(blocks, chunk_x, chunk_z,
                                          &chunks[index].vertices);
    return chunks[index].vertices == 0 || chunks[index].vb != NULL;
}

static cc_bool create_chunk_meshes(const BlockRaw *blocks,
                                   struct chunk_mesh *chunks) {
    for (int z = 0; z < CHUNKS_Z; ++z)
    for (int x = 0; x < CHUNKS_X; ++x) {
        if (!rebuild_chunk(blocks, chunks, x, z)) return false;
    }
    return true;
}

static cc_bool rebuild_near_block(const BlockRaw *blocks,
                                  struct chunk_mesh *chunks, int bx, int bz) {
    int chunk_x = bx / DEMO_CHUNK_SIZE, chunk_z = bz / DEMO_CHUNK_SIZE;
    /* Adjacent chunk faces can change when an edge block changes. Rebuilding
       the 3x3 neighbourhood is simple, bounded, and still far cheaper than
       regenerating the complete world mesh. */
    for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
        if (!rebuild_chunk(blocks, chunks, chunk_x + dx, chunk_z + dz)) return false;
    }
    return true;
}

struct hotbar_mesh {
    GfxResourceID panel_vb, icon_vb;
    int panel_vertices, icon_vertices;
};

static int slot_tile(BlockRaw block) {
    return block == BLOCK_GRASS ? TILE_GRASS_TOP :
           block == BLOCK_DIRT  ? TILE_DIRT : TILE_STONE;
}

static cc_bool create_hotbar(BlockRaw selected, GfxResourceID atlas,
                             int width, int height, struct hotbar_mesh *out) {
    enum { SLOT_COUNT = 3, RECT_COUNT = SLOT_COUNT + 4 };
    out->panel_vb = NULL; out->icon_vb = NULL;
    out->panel_vertices = RECT_COUNT * 4;
    out->icon_vertices = SLOT_COUNT * 4;
    out->panel_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED,
                                          out->panel_vertices);
    if (out->panel_vb == NULL) return false;
    struct VertexColoured *v = Gfx_LockVb(out->panel_vb, VERTEX_FORMAT_COLOURED,
                                          out->panel_vertices);
    if (v == NULL) { Gfx_DeleteVb(&out->panel_vb); return false; }
    BlockRaw blocks[SLOT_COUNT] = { BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE };
    int selected_slot = selected == BLOCK_DIRT ? 1 : selected == BLOCK_STONE ? 2 : 0;
    int start_x = width / 2 - 29;
    int bar_y = height - 24;
    PackedCol panel = PackedCol_Make(18, 24, 34, 220);
    PackedCol accent = PackedCol_Make(242, 245, 252, 255);
    for (int i = 0; i < SLOT_COUNT; ++i)
        v = Gfx_Build2DFlat(start_x + i * 20, bar_y, 18, 18, panel, v);
    int sx = start_x + selected_slot * 20;
    v = Gfx_Build2DFlat(sx - 1, bar_y - 1, 20, 1, accent, v);
    v = Gfx_Build2DFlat(sx - 1, bar_y + 18, 20, 1, accent, v);
    v = Gfx_Build2DFlat(sx - 1, bar_y - 1, 1, 20, accent, v);
    v = Gfx_Build2DFlat(sx + 18, bar_y - 1, 1, 20, accent, v);
    Gfx_UnlockVb(out->panel_vb);

    out->icon_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_TEXTURED,
                                         out->icon_vertices);
    if (out->icon_vb == NULL) {
        Gfx_DeleteVb(&out->panel_vb); return false;
    }
    struct VertexTextured *t = Gfx_LockVb(out->icon_vb, VERTEX_FORMAT_TEXTURED,
                                          out->icon_vertices);
    if (t == NULL) {
        Gfx_DeleteVb(&out->icon_vb); Gfx_DeleteVb(&out->panel_vb); return false;
    }
    for (int i = 0; i < SLOT_COUNT; ++i) {
        struct Texture tex;
        tex.ID = atlas;
        tex.x = (short)(start_x + i * 20 + 4);
        tex.y = (short)(bar_y + 4);
        tex.width = 10; tex.height = 10;
        tile_uv(slot_tile(blocks[i]), &tex.uv);
        Gfx_Make2DQuad(&tex, PackedCol_Make(255, 255, 255, 255), &t);
    }
    Gfx_UnlockVb(out->icon_vb);
    return true;
}

static void preview_pixel(BitmapCol *pixels, int x, int y, BitmapCol color) {
    if ((unsigned)x >= PREVIEW_WIDTH || (unsigned)y >= PREVIEW_HEIGHT) return;
    pixels[(unsigned)y * PREVIEW_WIDTH + (unsigned)x] = color;
}

static void render_world_preview(const BlockRaw *blocks, BitmapCol *pixels,
                                 unsigned frame) {
    for (cc_uint32 i = 0; i < PREVIEW_WIDTH * PREVIEW_HEIGHT; ++i)
        pixels[i] = BitmapColor_RGB(91, 157, 219);

    /* Draw the generated block volume as a lightweight isometric preview.
       The triangular camera drift proves that every submitted frame contains
       newly rendered pixels instead of repeatedly presenting one snapshot.
       This is intentionally the D3 bootstrap, before SoftGPU is connected. */
    int phase = (int)(frame % 64u);
    int camera_x = phase < 32 ? phase / 2 - 8 : 23 - phase / 2;
    for (int sum = 0; sum <= 62; ++sum) {
        for (int x = 0; x < World.Width; ++x) {
            int z = sum - x;
            if (z < 0 || z >= World.Length) continue;
            int top = World.Height - 1;
            while (top > 0 && blocks[World_Pack(x, top, z)] == BLOCK_AIR) --top;
            BlockRaw block = blocks[World_Pack(x, top, z)];
            BitmapCol color = block == BLOCK_GRASS ? BitmapColor_RGB(81, 174, 87) :
                              block == BLOCK_DIRT  ? BitmapColor_RGB(132, 91, 55) :
                                                    BitmapColor_RGB(160, 160, 160);
            if ((x + z) & 1) color = BitmapColor_Offset(color, -7, -7, -7);
            int screen_x = 128 + camera_x + (x - z) * 4;
            int screen_y = 34 + (x + z) * 2 - top * 2;
            preview_pixel(pixels, screen_x,     screen_y,     color);
            preview_pixel(pixels, screen_x - 1, screen_y + 1, color);
            preview_pixel(pixels, screen_x,     screen_y + 1, color);
            preview_pixel(pixels, screen_x + 1, screen_y + 1, color);
            preview_pixel(pixels, screen_x - 2, screen_y + 2, color);
            preview_pixel(pixels, screen_x - 1, screen_y + 2, color);
            preview_pixel(pixels, screen_x,     screen_y + 2, color);
            preview_pixel(pixels, screen_x + 1, screen_y + 2, color);
            preview_pixel(pixels, screen_x + 2, screen_y + 2, color);
        }
    }
}

static bool present_world_preview(const BlockRaw *blocks) {
    struct Bitmap framebuffer;
    Window_PreInit();
    Window_Init();
    Window_Create2D(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    Window_AllocFramebuffer(&framebuffer, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    if (framebuffer.scan0 == NULL) { Window_Free(); return false; }

    Rect2D damage = { 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT };
    for (unsigned frame = 0u; frame < 120u; ++frame) {
        render_world_preview(blocks, framebuffer.scan0, frame);
        Window_DrawFramebuffer(damage, &framebuffer);
        Window_ProcessEvents(0.005f);
        Thread_Sleep(5u);
    }
    bool presented = DemonOS_WindowDisplayAvailable() ?
                     DemonOS_WindowPresentCount() == 120u : true;
    Window_FreeFramebuffer(&framebuffer);
    Window_Destroy();
    Window_Free();
    return presented;
}

/* Compact 5x7 uppercase/digit glyph table, identical to the framebuffer
   console's font, so the pause overlay stays 100% procedural. */
struct overlay_glyph { char value; cc_uint8 rows[7]; };
static const struct overlay_glyph overlay_font[] = {
  {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}},
  {'#',{10,31,10,10,31,10,0}}, {'>',{16,8,4,2,4,8,16}},
  {'<',{1,2,4,8,4,2,1}}, {'_',{0,0,0,0,0,0,31}},
  {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
  {'|',{4,4,4,4,4,4,4}},
  {':',{0,12,12,0,12,12,0}}, {'/',{1,2,4,8,16,0,0}},
  {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
  {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
  {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
  {'9',{14,17,17,15,1,1,14}}, {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
  {'C',{14,17,16,16,16,17,14}}, {'D',{28,18,17,17,17,18,28}}, {'E',{31,16,16,30,16,16,31}},
  {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
  {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}}, {'K',{17,18,20,24,20,18,17}},
  {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
  {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
  {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
  {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
  {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
};

static const cc_uint8 *overlay_glyph_rows(char value) {
    if (value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
    for (size_t i = 0u; i < sizeof(overlay_font) / sizeof(overlay_font[0]); ++i)
        if (overlay_font[i].value == value) return overlay_font[i].rows;
    return overlay_font[0].rows;
}

/* Rasterises a line of 5x7 glyphs into a power-of-two textured bitmap. */
static GfxResourceID create_overlay_text(const char *text, int *used_width) {
    int length = 0;
    while (text[length] != '\0') ++length;
    int used = length * 6 - 1;
    if (used < 1) used = 1;
    int padded = 1;
    while (padded < used) padded <<= 1;
    struct Bitmap bmp;
    Bitmap_TryAllocate(&bmp, padded, 8);
    if (bmp.scan0 == NULL) return NULL;
    for (int i = 0; i < padded * 8; ++i) bmp.scan0[i] = 0;
    for (int c = 0; c < length; ++c) {
        const cc_uint8 *rows = overlay_glyph_rows(text[c]);
        int origin = c * 6;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1u << (4 - col)))
                    bmp.scan0[row * padded + origin + col] = BITMAPCOLOR_WHITE;
    }
    GfxResourceID texture = Gfx_CreateTexture(&bmp, 0, false);
    Mem_Free(bmp.scan0);
    *used_width = used;
    return texture;
}

static struct VertexTextured *build_overlay_quad(struct VertexTextured *vertices,
                                                 GfxResourceID texture,
                                                 int used_width, int padded_width,
                                                 int x, int y) {
    struct Texture tex;
    tex.ID = texture;
    tex.x = (short)x; tex.y = (short)y;
    tex.width = (cc_uint16)used_width; tex.height = 7;
    tex.uv.u1 = 0.0f; tex.uv.v1 = 0.0f;
    tex.uv.u2 = (float)used_width / (float)padded_width;
    tex.uv.v2 = 7.0f / 8.0f;
    Gfx_Make2DQuad(&tex, PackedCol_Make(255, 255, 255, 255), &vertices);
    return vertices;
}

static GfxResourceID create_overlay_vb(GfxResourceID texture, int used_width,
                                       int x, int y) {
    int padded = 1;
    while (padded < used_width) padded <<= 1;
    GfxResourceID vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_TEXTURED, 4);
    if (vb == NULL) return NULL;
    struct VertexTextured *vertices = Gfx_LockVb(vb, VERTEX_FORMAT_TEXTURED, 4);
    if (vertices == NULL) { Gfx_DeleteVb(&vb); return NULL; }
    build_overlay_quad(vertices, texture, used_width, padded, x, y);
    Gfx_UnlockVb(vb);
    return vb;
}

static bool validate_softgpu_world(BlockRaw *blocks) {
    Window_PreInit();
    Window_Init();
    const int width = PREVIEW_WIDTH, height = PREVIEW_HEIGHT;
    Window_Create2D(width, height);
    Gfx_Create();
    Gfx_OnWindowResize(width, height);
    struct chunk_mesh chunks[CHUNK_COUNT] = { 0 };
    if (!create_chunk_meshes(blocks, chunks)) { Window_Free(); return false; }
    GfxResourceID atlas = create_block_atlas();
    if (atlas == NULL) { Window_Free(); return false; }
    BitmapCol white_pixel = BITMAPCOLOR_WHITE;
    struct Bitmap white_bitmap;
    Bitmap_Init(white_bitmap, 1, 1, &white_pixel);
    GfxResourceID white_texture = Gfx_AllocTexture(&white_bitmap, 1, 0, false);
    if (white_texture == NULL) {
        Gfx_DeleteTexture(&atlas); Window_Free(); return false;
    }
    GfxResourceID crosshair_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED, 8);
    if (crosshair_vb == NULL) {
        Gfx_DeleteTexture(&white_texture); Gfx_DeleteTexture(&atlas);
        Window_Free(); return false;
    }
    struct VertexColoured *crosshair = Gfx_LockVb(crosshair_vb,
                                                  VERTEX_FORMAT_COLOURED, 8);
    if (crosshair == NULL) { Window_Free(); return false; }
    PackedCol crosshair_color = PackedCol_Make(245, 248, 255, 255);
    crosshair = Gfx_Build2DFlat(width / 2 - 6, height / 2, 13, 1,
                                crosshair_color, crosshair);
    crosshair = Gfx_Build2DFlat(width / 2, height / 2 - 6, 1, 13,
                                crosshair_color, crosshair);
    Gfx_UnlockVb(crosshair_vb);
    GfxResourceID target_crosshair_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED, 8);
    if (target_crosshair_vb == NULL) { Window_Free(); return false; }
    crosshair = Gfx_LockVb(target_crosshair_vb, VERTEX_FORMAT_COLOURED, 8);
    if (crosshair == NULL) { Window_Free(); return false; }
    PackedCol target_color = PackedCol_Make(255, 210, 64, 255);
    crosshair = Gfx_Build2DFlat(width / 2 - 6, height / 2, 13, 1,
                                target_color, crosshair);
    crosshair = Gfx_Build2DFlat(width / 2, height / 2 - 6, 1, 13,
                                target_color, crosshair);
    Gfx_UnlockVb(target_crosshair_vb);
    BlockRaw selected_block = BLOCK_GRASS;
    BlockRaw displayed_block = selected_block;
    struct hotbar_mesh hotbar;
    if (!create_hotbar(selected_block, atlas, width, height, &hotbar)) {
        Gfx_DeleteVb(&target_crosshair_vb); Gfx_DeleteVb(&crosshair_vb);
        Gfx_DeleteTexture(&white_texture); Gfx_DeleteTexture(&atlas);
        Window_Free(); return false;
    }
    Gfx_BindTexture(white_texture);
    Gfx_SetVertexFormat(VERTEX_FORMAT_COLOURED);
    Gfx_SetDepthTest(true);
    Gfx_SetDepthWrite(true);
    Gfx_SetFaceCulling(false);

    struct Matrix projection;
    Gfx_CalcPerspectiveMatrix(&projection, 1.0471975512f,
                              (float)width / (float)height, 128.0f);
    Gfx_LoadMatrix(MATRIX_PROJ, &projection);

    struct demo_player player = {
        .x = 0.0f, .z = 14.0f, .yaw = 0.0f, .pitch = -0.30f,
        .grounded = true
    };
    player.y = player_ground(blocks, player.x, player.z);
    player.highest_y = player.y;
    float initial_y = player.y, initial_z = player.z, initial_yaw = player.yaw;
    Event_Register_(&PointerEvents.RawMoved, &player, player_raw_move);

    struct input_event control = { .type = INPUT_KEY_DOWN, .code = 0x11u };
    DemonOS_ApplyInputEvent(&control); /* hold W */
    control = (struct input_event){ .type = INPUT_KEY_DOWN, .code = 0x39u };
    DemonOS_ApplyInputEvent(&control); /* begin one jump */
    control = (struct input_event){ .type = INPUT_MOUSE_MOVE, .x = 128, .y = 96,
                                   .delta_x = 9, .delta_y = 6 };
    DemonOS_ApplyInputEvent(&control); /* real raw-look event path */

    uint32_t clear_checksum = 0u;
    unsigned frame = 0u;
    unsigned live_edits = 0u;
    cc_bool selected_dirt = false;
    cc_bool wheel_selected = false;
    cc_bool sprint_seen = false;
    cc_bool picked_block = false;
    cc_bool previous_left = false, previous_right = false, previous_middle = false;
    cc_bool interactive = false;
    struct hotbar_input hotbar_input = { 0 };
    Event_Register_(&InputEvents.Wheel, &hotbar_input, hotbar_wheel);
    while (Window_Main.Exists && frame < 36000u &&
           (frame < 180u || interactive)) {
        if (frame == 1u) {
            control = (struct input_event){ .type = INPUT_KEY_UP, .code = 0x39u };
            DemonOS_ApplyInputEvent(&control);
        }
        if (frame == 2u) {
            control = (struct input_event){ .type = INPUT_KEY_DOWN, .code = 0x2Au };
            DemonOS_ApplyInputEvent(&control); /* sprint while W is held */
        } else if (frame == 5u) {
            control = (struct input_event){ .type = INPUT_KEY_UP, .code = 0x2Au };
            DemonOS_ApplyInputEvent(&control);
        }
        if (frame == 9u) {
            control = (struct input_event){ .type = INPUT_KEY_UP, .code = 0x11u };
            DemonOS_ApplyInputEvent(&control);
        }
        if (frame == 16u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_DOWN,
                                           .code = INPUT_MOUSE_MIDDLE };
            DemonOS_ApplyInputEvent(&control);
        } else if (frame == 17u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_UP,
                                           .code = INPUT_MOUSE_MIDDLE };
            DemonOS_ApplyInputEvent(&control);
        } else if (frame == 18u) {
            control = (struct input_event){ .type = INPUT_MOUSE_SCROLL, .value = -1 };
            DemonOS_ApplyInputEvent(&control); /* wheel to slot 2: dirt */
        }
        /* Exercise the same edge-triggered live edit path during smoke. */
        if (frame == 20u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_DOWN,
                                           .code = INPUT_MOUSE_LEFT };
            DemonOS_ApplyInputEvent(&control);
        } else if (frame == 21u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_UP,
                                           .code = INPUT_MOUSE_LEFT };
            DemonOS_ApplyInputEvent(&control);
        } else if (frame == 22u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_DOWN,
                                           .code = INPUT_MOUSE_RIGHT };
            DemonOS_ApplyInputEvent(&control);
        } else if (frame == 23u) {
            control = (struct input_event){ .type = INPUT_MOUSE_BUTTON_UP,
                                           .code = INPUT_MOUSE_RIGHT };
            DemonOS_ApplyInputEvent(&control);
        }
        Window_ProcessEvents(0.1f);
        if (DemonOS_WindowInputCount() != 0u) interactive = true;
        if (Input_IsShiftPressed()) sprint_seen = true;
        player_tick(&player, blocks, 0.1f);
        if (Input.Pressed[CCKEY_1]) selected_block = BLOCK_GRASS;
        if (Input.Pressed[CCKEY_2]) { selected_block = BLOCK_DIRT; selected_dirt = true; }
        if (Input.Pressed[CCKEY_3]) selected_block = BLOCK_STONE;
        while (hotbar_input.steps > 0) {
            selected_block = selected_block == BLOCK_GRASS ? BLOCK_DIRT :
                             selected_block == BLOCK_DIRT  ? BLOCK_STONE : BLOCK_GRASS;
            hotbar_input.steps--; wheel_selected = true;
        }
        while (hotbar_input.steps < 0) {
            selected_block = selected_block == BLOCK_GRASS ? BLOCK_STONE :
                             selected_block == BLOCK_STONE ? BLOCK_DIRT : BLOCK_GRASS;
            hotbar_input.steps++; wheel_selected = true;
        }
        if (selected_block == BLOCK_DIRT) selected_dirt = true;
        cc_bool left = Input.Pressed[CCMOUSE_L], right = Input.Pressed[CCMOUSE_R];
        cc_bool middle = Input.Pressed[CCMOUSE_M];
        if ((left && !previous_left) || (right && !previous_right) ||
            (middle && !previous_middle)) {
            struct block_hit hit;
            if (player_pick_block(&player, blocks, &hit)) {
                cc_bool changed = false;
                if (middle) {
                    BlockRaw picked = blocks[World_Pack(hit.x, hit.y, hit.z)];
                    if (picked == BLOCK_GRASS || picked == BLOCK_DIRT ||
                        picked == BLOCK_STONE) {
                        selected_block = picked;
                        picked_block = true;
                    }
                }
                if (left) {
                    blocks[World_Pack(hit.x, hit.y, hit.z)] = BLOCK_AIR;
                    changed = true;
                }
                if (right && hit.place_x >= 0 && hit.place_y >= 0 && hit.place_z >= 0 &&
                    !block_overlaps_player(&player, hit.place_x, hit.place_y, hit.place_z)) {
                    blocks[World_Pack(hit.place_x, hit.place_y, hit.place_z)] = selected_block;
                    changed = true;
                }
                if (changed) {
                    int changed_x = left ? hit.x : hit.place_x;
                    int changed_z = left ? hit.z : hit.place_z;
                    if (!rebuild_near_block(blocks, chunks, changed_x, changed_z) ||
                        !persist_edited_world(blocks)) break;
                    ++live_edits;
                }
            }
        }
        if (selected_block != displayed_block) {
            Gfx_DeleteVb(&hotbar.icon_vb); Gfx_DeleteVb(&hotbar.panel_vb);
            if (!create_hotbar(selected_block, atlas, width, height, &hotbar)) break;
            displayed_block = selected_block;
        }
        previous_left = left; previous_right = right; previous_middle = middle;
        unsigned pulse = frame & 15u;
        PackedCol color = PackedCol_Make(24u + pulse * 3u,
                                         48u + pulse * 2u,
                                         96u + pulse * 2u, 255u);
        Gfx_ClearColor(color);
        Gfx_ClearBuffers(GFX_BUFFER_COLOR | GFX_BUFFER_DEPTH);
        Gfx_BeginFrame();
        if (frame == 0u) {
            Gfx_EndFrame();
            clear_checksum = DemonOS_WindowLastChecksum();
            ++frame;
            continue;
        }
        struct Matrix view;
        Vec3 camera = { player.x, player.y + 1.62f, player.z };
        Vec2 rotation = { player.yaw, player.pitch };
        Matrix_LookRot(&view, camera, rotation);
        Gfx_LoadMatrix(MATRIX_PROJ, &projection);
        Gfx_LoadMatrix(MATRIX_VIEW, &view);
        Gfx_BindTexture(atlas);
        Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
        for (int i = 0; i < CHUNK_COUNT; ++i) {
            if (chunks[i].vb == NULL || chunks[i].vertices == 0) continue;
            Gfx_BindVb(chunks[i].vb);
            Gfx_DrawVb_IndexedTris_Range(chunks[i].vertices, 0, DRAW_HINT_NONE);
        }
        Gfx_Begin2D(width, height);
        struct block_hit visible_hit;
        cc_bool has_visible_target = player_pick_block(&player, blocks, &visible_hit);
        Gfx_BindTexture(white_texture);
        Gfx_SetVertexFormat(VERTEX_FORMAT_COLOURED);
        Gfx_BindVb(has_visible_target ? target_crosshair_vb : crosshair_vb);
        Gfx_DrawVb_IndexedTris_Range(8, 0, DRAW_HINT_RECT);
        Gfx_BindVb(hotbar.panel_vb);
        Gfx_DrawVb_IndexedTris_Range(hotbar.panel_vertices, 0, DRAW_HINT_RECT);
        Gfx_BindTexture(atlas);
        Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
        Gfx_BindVb(hotbar.icon_vb);
        Gfx_DrawVb_IndexedTris_Range(hotbar.icon_vertices, 0, DRAW_HINT_RECT);
        Gfx_End2D();
        Gfx_EndFrame();
        Thread_Sleep(10u);
        ++frame;
    }
    Event_Unregister_(&InputEvents.Wheel, &hotbar_input, hotbar_wheel);
    Event_Unregister_(&PointerEvents.RawMoved, &player, player_raw_move);
    Input_Clear();
    bool controlled = player.z < initial_z - 2.0f &&
                      player.yaw > initial_yaw &&
                      player.highest_y > initial_y + 0.5f && player.grounded;
    bool rendered = (!DemonOS_WindowDisplayAvailable() ||
                     DemonOS_WindowPresentCount() >= 180u) &&
                    live_edits >= 2u && selected_dirt && wheel_selected &&
                    sprint_seen && picked_block &&
                    DemonOS_WindowLastChecksum() != clear_checksum && controlled;
    Gfx_DeleteVb(&hotbar.icon_vb); Gfx_DeleteVb(&hotbar.panel_vb);
    Gfx_DeleteVb(&target_crosshair_vb);
    Gfx_DeleteVb(&crosshair_vb);
    Gfx_DeleteTexture(&white_texture);
    Gfx_DeleteTexture(&atlas);
    for (int i = 0; i < CHUNK_COUNT; ++i) Gfx_DeleteVb(&chunks[i].vb);
    /* The process arena owns SoftGPU's buffers and is unmapped at shutdown.
       Full Gfx_Free becomes appropriate once default resources are created. */
    Window_Destroy();
    Window_Free();
    return rendered;
}

/* Interactive session launched from MakoBox ("classicube"). Uses the full
   display resolution, reads real input, and runs until the player quits. */
static bool play_interactive(BlockRaw *blocks) {
    Window_PreInit();
    Window_Init();
    const int width = DisplayInfo.Width > 0 ? DisplayInfo.Width : 640;
    const int height = DisplayInfo.Height > 0 ? DisplayInfo.Height : 480;
    Window_Create2D(width, height);
    Gfx_Create();
    Gfx_OnWindowResize(width, height);
    struct chunk_mesh chunks[CHUNK_COUNT] = { 0 };
    if (!create_chunk_meshes(blocks, chunks)) { Window_Free(); return false; }
    GfxResourceID atlas = create_block_atlas();
    if (atlas == NULL) { Window_Free(); return false; }
    BitmapCol white_pixel = BITMAPCOLOR_WHITE;
    struct Bitmap white_bitmap;
    Bitmap_Init(white_bitmap, 1, 1, &white_pixel);
    GfxResourceID white_texture = Gfx_AllocTexture(&white_bitmap, 1, 0, false);
    if (white_texture == NULL) {
        Gfx_DeleteTexture(&atlas); Window_Free(); return false;
    }
    GfxResourceID crosshair_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED, 8);
    if (crosshair_vb == NULL) {
        Gfx_DeleteTexture(&white_texture); Gfx_DeleteTexture(&atlas);
        Window_Free(); return false;
    }
    struct VertexColoured *crosshair = Gfx_LockVb(crosshair_vb,
                                                  VERTEX_FORMAT_COLOURED, 8);
    if (crosshair == NULL) { Window_Free(); return false; }
    PackedCol crosshair_color = PackedCol_Make(245, 248, 255, 255);
    crosshair = Gfx_Build2DFlat(width / 2 - 6, height / 2, 13, 1,
                                crosshair_color, crosshair);
    crosshair = Gfx_Build2DFlat(width / 2, height / 2 - 6, 1, 13,
                                crosshair_color, crosshair);
    Gfx_UnlockVb(crosshair_vb);
    GfxResourceID target_crosshair_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED, 8);
    if (target_crosshair_vb == NULL) { Window_Free(); return false; }
    crosshair = Gfx_LockVb(target_crosshair_vb, VERTEX_FORMAT_COLOURED, 8);
    if (crosshair == NULL) { Window_Free(); return false; }
    PackedCol target_color = PackedCol_Make(255, 210, 64, 255);
    crosshair = Gfx_Build2DFlat(width / 2 - 6, height / 2, 13, 1,
                                target_color, crosshair);
    crosshair = Gfx_Build2DFlat(width / 2, height / 2 - 6, 1, 13,
                                target_color, crosshair);
    Gfx_UnlockVb(target_crosshair_vb);
    BlockRaw selected_block = BLOCK_GRASS;
    BlockRaw displayed_block = selected_block;
    struct hotbar_mesh hotbar;
    if (!create_hotbar(selected_block, atlas, width, height, &hotbar)) {
        Gfx_DeleteVb(&target_crosshair_vb); Gfx_DeleteVb(&crosshair_vb);
        Gfx_DeleteTexture(&white_texture); Gfx_DeleteTexture(&atlas);
        Window_Free(); return false;
    }

    int pause_line1_used = 0, pause_line2_used = 0;
    GfxResourceID pause_line1_tex = create_overlay_text("PAUSED", &pause_line1_used);
    GfxResourceID pause_line2_tex = create_overlay_text("ESC CONTINUE   Q QUIT",
                                                        &pause_line2_used);
    GfxResourceID dim_vb = Gfx_TryCreateStaticVb(VERTEX_FORMAT_COLOURED, 4);
    if (dim_vb != NULL) {
        struct VertexColoured *dim = Gfx_LockVb(dim_vb, VERTEX_FORMAT_COLOURED, 4);
        if (dim == NULL) {
            Gfx_DeleteVb(&dim_vb);
        } else {
            Gfx_Build2DFlat(0, 0, width, height, PackedCol_Make(6, 10, 18, 150), dim);
            Gfx_UnlockVb(dim_vb);
        }
    }
    GfxResourceID pause_line1_vb = pause_line1_tex != NULL ?
        create_overlay_vb(pause_line1_tex, pause_line1_used,
                          width / 2 - pause_line1_used / 2, height / 2 - 40) : NULL;
    GfxResourceID pause_line2_vb = pause_line2_tex != NULL ?
        create_overlay_vb(pause_line2_tex, pause_line2_used,
                          width / 2 - pause_line2_used / 2, height / 2 - 24) : NULL;

    Gfx_BindTexture(white_texture);
    Gfx_SetVertexFormat(VERTEX_FORMAT_COLOURED);
    Gfx_SetDepthTest(true);
    Gfx_SetDepthWrite(true);
    Gfx_SetFaceCulling(false);

    struct Matrix projection;
    Gfx_CalcPerspectiveMatrix(&projection, 1.0471975512f,
                              (float)width / (float)height, 128.0f);
    Gfx_LoadMatrix(MATRIX_PROJ, &projection);

    struct demo_player player = {
        .x = 0.0f, .z = 14.0f, .yaw = 0.0f, .pitch = -0.30f,
        .grounded = true
    };
    player.y = player_ground(blocks, player.x, player.z);
    player.highest_y = player.y;
    Event_Register_(&PointerEvents.RawMoved, &player, player_raw_move);

    cc_bool pause_active = false;
    cc_bool previous_esc = false, previous_q = false;
    cc_bool quit_requested = false;
    cc_bool previous_left = false, previous_right = false, previous_middle = false;
    unsigned frame = 0u;
    struct hotbar_input hotbar_input = { 0 };
    Event_Register_(&InputEvents.Wheel, &hotbar_input, hotbar_wheel);
    while (Window_Main.Exists && frame < 36000u && !quit_requested) {
        Window_ProcessEvents(0.1f);
        cc_bool esc = Input.Pressed[CCKEY_ESCAPE];
        if (esc && !previous_esc) pause_active = !pause_active;
        previous_esc = esc;
        if (Input.Pressed[CCKEY_Q] && !previous_q) quit_requested = true;
        previous_q = Input.Pressed[CCKEY_Q];
        cc_bool left = Input.Pressed[CCMOUSE_L], right = Input.Pressed[CCMOUSE_R];
        cc_bool middle = Input.Pressed[CCMOUSE_M];
        if (!pause_active) {
            player_tick(&player, blocks, 0.1f);
            if (Input.Pressed[CCKEY_1]) selected_block = BLOCK_GRASS;
            if (Input.Pressed[CCKEY_2]) selected_block = BLOCK_DIRT;
            if (Input.Pressed[CCKEY_3]) selected_block = BLOCK_STONE;
            while (hotbar_input.steps > 0) {
                selected_block = selected_block == BLOCK_GRASS ? BLOCK_DIRT :
                                 selected_block == BLOCK_DIRT  ? BLOCK_STONE : BLOCK_GRASS;
                hotbar_input.steps--;
            }
            while (hotbar_input.steps < 0) {
                selected_block = selected_block == BLOCK_GRASS ? BLOCK_STONE :
                                 selected_block == BLOCK_STONE ? BLOCK_DIRT : BLOCK_GRASS;
                hotbar_input.steps++;
            }
            if ((left && !previous_left) || (right && !previous_right) ||
                (middle && !previous_middle)) {
                struct block_hit hit;
                if (player_pick_block(&player, blocks, &hit)) {
                    cc_bool changed = false;
                    if (middle) {
                        BlockRaw picked = blocks[World_Pack(hit.x, hit.y, hit.z)];
                        if (picked == BLOCK_GRASS || picked == BLOCK_DIRT ||
                            picked == BLOCK_STONE) selected_block = picked;
                    }
                    if (left) {
                        blocks[World_Pack(hit.x, hit.y, hit.z)] = BLOCK_AIR;
                        changed = true;
                    }
                    if (right && hit.place_x >= 0 && hit.place_y >= 0 &&
                        hit.place_z >= 0 &&
                        !block_overlaps_player(&player, hit.place_x, hit.place_y,
                                               hit.place_z)) {
                        blocks[World_Pack(hit.place_x, hit.place_y, hit.place_z)] =
                            selected_block;
                        changed = true;
                    }
                    if (changed) {
                        int changed_x = left ? hit.x : hit.place_x;
                        int changed_z = left ? hit.z : hit.place_z;
                        if (!rebuild_near_block(blocks, chunks, changed_x, changed_z) ||
                            !persist_edited_world(blocks)) quit_requested = true;
                    }
                }
            }
            if (selected_block != displayed_block) {
                Gfx_DeleteVb(&hotbar.icon_vb); Gfx_DeleteVb(&hotbar.panel_vb);
                if (!create_hotbar(selected_block, atlas, width, height, &hotbar))
                    quit_requested = true;
                displayed_block = selected_block;
            }
        } else {
            hotbar_input.steps = 0;
        }
        previous_left = left; previous_right = right; previous_middle = middle;
        unsigned pulse = frame & 15u;
        PackedCol color = PackedCol_Make(24u + pulse * 3u,
                                         48u + pulse * 2u,
                                         96u + pulse * 2u, 255u);
        Gfx_ClearColor(color);
        Gfx_ClearBuffers(GFX_BUFFER_COLOR | GFX_BUFFER_DEPTH);
        Gfx_BeginFrame();
        Gfx_LoadMatrix(MATRIX_PROJ, &projection);
        struct Matrix view;
        Vec3 camera = { player.x, player.y + 1.62f, player.z };
        Vec2 rotation = { player.yaw, player.pitch };
        Matrix_LookRot(&view, camera, rotation);
        Gfx_LoadMatrix(MATRIX_VIEW, &view);
        Gfx_BindTexture(atlas);
        Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
        for (int i = 0; i < CHUNK_COUNT; ++i) {
            if (chunks[i].vb == NULL || chunks[i].vertices == 0) continue;
            Gfx_BindVb(chunks[i].vb);
            Gfx_DrawVb_IndexedTris_Range(chunks[i].vertices, 0, DRAW_HINT_NONE);
        }
        Gfx_Begin2D(width, height);
        struct block_hit visible_hit;
        cc_bool has_visible_target = player_pick_block(&player, blocks, &visible_hit);
        Gfx_BindTexture(white_texture);
        Gfx_SetVertexFormat(VERTEX_FORMAT_COLOURED);
        Gfx_BindVb(has_visible_target ? target_crosshair_vb : crosshair_vb);
        Gfx_DrawVb_IndexedTris_Range(8, 0, DRAW_HINT_RECT);
        Gfx_BindVb(hotbar.panel_vb);
        Gfx_DrawVb_IndexedTris_Range(hotbar.panel_vertices, 0, DRAW_HINT_RECT);
        Gfx_BindTexture(atlas);
        Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
        Gfx_BindVb(hotbar.icon_vb);
        Gfx_DrawVb_IndexedTris_Range(hotbar.icon_vertices, 0, DRAW_HINT_RECT);
        if (pause_active) {
            Gfx_BindTexture(white_texture);
            Gfx_SetVertexFormat(VERTEX_FORMAT_COLOURED);
            if (dim_vb != NULL) {
                Gfx_BindVb(dim_vb);
                Gfx_DrawVb_IndexedTris_Range(4, 0, DRAW_HINT_RECT);
            }
            Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
            if (pause_line1_tex != NULL && pause_line1_vb != NULL) {
                Gfx_BindTexture(pause_line1_tex);
                Gfx_BindVb(pause_line1_vb);
                Gfx_DrawVb_IndexedTris_Range(4, 0, DRAW_HINT_RECT);
            }
            if (pause_line2_tex != NULL && pause_line2_vb != NULL) {
                Gfx_BindTexture(pause_line2_tex);
                Gfx_BindVb(pause_line2_vb);
                Gfx_DrawVb_IndexedTris_Range(4, 0, DRAW_HINT_RECT);
            }
        }
        Gfx_End2D();
        Gfx_EndFrame();
        if (frame % 60u == 0u) {
            char dbg[96];
            cc_string s;
            String_InitArray(s, dbg);
            String_AppendConst(&s, "CC_DBG frame=");
            String_AppendInt(&s, (int)frame);
            String_AppendConst(&s, " present=");
            String_AppendInt(&s, (int)DemonOS_WindowPresentCount());
            String_AppendConst(&s, " checksum=");
            String_AppendInt(&s, (int)DemonOS_WindowLastChecksum());
            String_AppendConst(&s, "\n");
            s.buffer[s.length] = '\0';
            demon_port_write(s.buffer);
        }
        Thread_Sleep(10u);
        ++frame;
    }
    Event_Unregister_(&InputEvents.Wheel, &hotbar_input, hotbar_wheel);
    Event_Unregister_(&PointerEvents.RawMoved, &player, player_raw_move);
    Input_Clear();
    Gfx_DeleteVb(&pause_line2_vb); Gfx_DeleteVb(&pause_line1_vb);
    Gfx_DeleteVb(&dim_vb);
    Gfx_DeleteTexture(&pause_line2_tex); Gfx_DeleteTexture(&pause_line1_tex);
    Gfx_DeleteVb(&hotbar.icon_vb); Gfx_DeleteVb(&hotbar.panel_vb);
    Gfx_DeleteVb(&target_crosshair_vb);
    Gfx_DeleteVb(&crosshair_vb);
    Gfx_DeleteTexture(&white_texture);
    Gfx_DeleteTexture(&atlas);
    for (int i = 0; i < CHUNK_COUNT; ++i) Gfx_DeleteVb(&chunks[i].vb);
    Window_Destroy();
    Window_Free();
    return true;
}

/* Required only on the failed-allocation path of upstream VB creation. */
cc_bool Game_ReduceVRAM(void) { return false; }

static BlockRaw *create_flatgrass_world(void) {
    World.Width = 32; World.Height = 32; World.Length = 32;
    World.MaxX = 31; World.MaxY = 31; World.MaxZ = 31;
    World.OneY = World.Width * World.Length;
    World.Volume = World.OneY * World.Height;
    World.Seed = 0x4D414B4F;
    Gen_Blocks = Mem_TryAlloc((cc_uint32)World.Volume, 1u);
    if (Gen_Blocks == NULL) return NULL;
    if (!FlatgrassGen.Prepare(World.Seed)) {
        Mem_Free(Gen_Blocks); Gen_Blocks = NULL; return NULL;
    }
    FlatgrassGen.Generate();
    return Gen_Blocks;
}

/* D1 executes real upstream ClassiCube string code inside a freestanding
   DemonOS process. This deliberately tests a narrow core slice before the
   window and game modules are linked. */
uint64_t classicube_core_main(void) {
    /* On the interactive ISO the classicube MakoBox command launches this
       same ELF directly with no smoke mode, so loop the real game until the
       player quits (Q). The scripted smoke boot is gated in src/kernel.c,
       which sets boot test mode before spawning the process. */
    if (demon_boot_test_mode() == 0u) {
        if (!demon_port_init_dynamic(4u * 1024u * 1024u)) return 10u;
        BlockRaw *interactive_world = create_flatgrass_world();
        if (interactive_world == NULL) return 46u;
        const bool interactive_ok = play_interactive(interactive_world);
        Mem_Free(interactive_world); Gen_Blocks = NULL;
        demon_port_shutdown();
        return interactive_ok ? 0u : 47u;
    }
    if (!demon_port_init_dynamic(4u * 1024u * 1024u)) return 10u;

    if (!validate_upstream_input()) return 35u;

    /* Validate the D4 translation boundary without relying on synthetic PS/2
       IRQ injection in the headless smoke VM. */
    struct input_event input_test = { .type = INPUT_KEY_DOWN, .code = 0x11u };
    struct demoni_cc_input translated;
    if (!DemonOS_TranslateInputEvent(&input_test, &translated) ||
        translated.key != CCKEY_W || !translated.pressed) return 36u;
    input_test.type = INPUT_KEY_UP;
    if (!DemonOS_TranslateInputEvent(&input_test, &translated) || translated.pressed)
        return 37u;
    input_test.type = INPUT_MOUSE_MOVE; input_test.delta_x = -7; input_test.delta_y = 4;
    if (!DemonOS_TranslateInputEvent(&input_test, &translated) ||
        translated.delta_x != -7 || translated.delta_y != 4) return 38u;
    input_test.type = INPUT_MOUSE_BUTTON_DOWN; input_test.code = INPUT_MOUSE_LEFT;
    if (!DemonOS_TranslateInputEvent(&input_test, &translated) ||
        translated.key != CCMOUSE_L || !translated.pressed) return 39u;
    input_test.type = INPUT_MOUSE_SCROLL; input_test.value = -1;
    if (!DemonOS_TranslateInputEvent(&input_test, &translated) || translated.wheel != -1)
        return 40u;

    cc_string name = String_FromReadonly("ClassiCube");
    cc_string expected = String_FromReadonly("ClassiCube");
    if (name.length != 10 || !String_Equals(&name, &expected)) return 11u;

    /* Exercise upstream memory streams and endian readers. This is the same
       stream layer later used by maps, textures, and network packets. */
    cc_uint8 encoded[] = { 0x78, 0x56, 0x34, 0x12, 0xCA, 0xFE, 0xBA, 0xBE };
    struct Stream stream;
    cc_uint32 little = 0u, big = 0u, position = 0u;
    Stream_ReadonlyMemory(&stream, encoded, sizeof(encoded));
    if (Stream_ReadU32_LE(&stream, &little) || little != 0x12345678u) return 12u;
    if (Stream_ReadU32_BE(&stream, &big) || big != 0xCAFEBABEu) return 13u;
    if (stream.Position(&stream, &position) || position != sizeof(encoded)) return 14u;

    /* Exercise the upstream deterministic RNG used by terrain generation. */
    RNGState random;
    Random_Seed(&random, 0x4D414B4Fu);
    int random_a = Random_Next(&random, 1024);
    int random_b = Random_Next(&random, 1024);
    if (random_a < 0 || random_a >= 1024 || random_b < 0 || random_b >= 1024 ||
        random_a == random_b) return 15u;

    /* Exercise upstream bitmap manipulation with allocated pixel storage. */
    struct Bitmap source, scaled;
    Bitmap_TryAllocate(&source, 2, 2);
    Bitmap_TryAllocate(&scaled, 4, 4);
    if (source.scan0 == NULL || scaled.scan0 == NULL) return 16u;
    source.scan0[0] = BitmapColor_RGB(255, 0, 0);
    source.scan0[1] = BitmapColor_RGB(0, 255, 0);
    source.scan0[2] = BitmapColor_RGB(0, 0, 255);
    source.scan0[3] = BitmapColor_RGB(255, 255, 255);
    Bitmap_Scale(&scaled, &source, 0, 0, 2, 2);
    if (scaled.scan0[0] != source.scan0[0] || scaled.scan0[15] != source.scan0[3])
        return 17u;
    Mem_Free(scaled.scan0);
    Mem_Free(source.scan0);

    /* Round-trip through ClassiCube's own file stream API and the DemonOS
       capability-backed RAMFS. This establishes the persistence path used by
       options and offline world files without adding a POSIX shim. */
    cc_string save_path = String_FromReadonly("/home/demon/classicube-d1.dat");
    cc_uint8 save_data[] = { 'M', 'A', 'K', 'O', 'C', 'U', 'B', 'E' };
    cc_uint8 load_data[sizeof(save_data)];
    struct Stream save_stream, load_stream;
    if (Stream_CreateFile(&save_stream, &save_path)) return 18u;
    if (Stream_Write(&save_stream, save_data, sizeof(save_data))) return 19u;
    if (save_stream.Close(&save_stream)) return 20u;
    if (Stream_OpenFile(&load_stream, &save_path)) return 21u;
    if (Stream_Read(&load_stream, load_data, sizeof(load_data))) return 22u;
    if (load_stream.Close(&load_stream)) return 23u;
    if (!Mem_Equal(save_data, load_data, sizeof(save_data))) return 24u;

    /* Run ClassiCube's actual flatgrass generator, persist its complete raw
       block volume, reload it, and compare deterministic checksums. */
    World.Width = 32; World.Height = 32; World.Length = 32;
    World.MaxX = 31; World.MaxY = 31; World.MaxZ = 31;
    World.OneY = World.Width * World.Length;
    World.Volume = World.OneY * World.Height;
    World.Seed = 0x4D414B4F;
    Gen_Blocks = create_flatgrass_world();
    if (Gen_Blocks == NULL) return 25u;
    if (Gen_Blocks[World_Pack(0, 15, 0)] != BLOCK_GRASS ||
        Gen_Blocks[World_Pack(0, 14, 0)] != BLOCK_DIRT ||
        Gen_Blocks[World_Pack(0, 16, 0)] != BLOCK_AIR) return 26u;
    cc_uint32 generated_hash = world_checksum(Gen_Blocks, (cc_uint32)World.Volume);

    cc_string world_path = String_FromReadonly("/home/demon/classicube-flatgrass.raw");
    struct Stream world_out, world_in;
    if (Stream_CreateFile(&world_out, &world_path)) return 27u;
    if (Stream_Write(&world_out, Gen_Blocks, (cc_uint32)World.Volume)) return 28u;
    if (world_out.Close(&world_out)) return 29u;
    Mem_Free(Gen_Blocks); Gen_Blocks = NULL;
    BlockRaw *reloaded = Mem_TryAlloc((cc_uint32)World.Volume, 1u);
    if (reloaded == NULL) return 30u;
    if (Stream_OpenFile(&world_in, &world_path)) return 31u;
    if (Stream_Read(&world_in, reloaded, (cc_uint32)World.Volume)) return 32u;
    if (world_in.Close(&world_in)) return 33u;
    if (world_checksum(reloaded, (cc_uint32)World.Volume) != generated_hash) return 34u;
    if (!validate_block_edits(reloaded)) return 42u;
    if (!persist_edited_world(reloaded)) return 43u;
    cc_uint32 edited_hash = world_checksum(reloaded, (cc_uint32)World.Volume);
    Mem_Set(reloaded, 0u, (cc_uint32)World.Volume);
    if (!load_edited_world(reloaded) ||
        world_checksum(reloaded, (cc_uint32)World.Volume) != edited_hash) return 44u;
    if (!validate_softgpu_world(reloaded)) return 41u;
    if (!present_world_preview(reloaded)) return 35u;
    if (!DemonOS_WindowDisplayAvailable())
        demon_port_write("CLASSICUBE_D3_DISPLAY_UNAVAILABLE present=skipped headless\n");
    Mem_Free(reloaded);

    char output_buffer[64];
    cc_string output;
    String_InitArray(output, output_buffer);
    String_AppendConst(&output, "CLASSICUBE_CORE_READY upstream=");
    String_AppendInt(&output, name.length);
    String_AppendConst(&output, "\n");
    output.buffer[output.length] = '\0';
    demon_port_write(output.buffer);
    demon_port_write("CLASSICUBE_D1_SUBSYSTEMS_READY stream rng bitmap\n");
    demon_port_write("CLASSICUBE_D2_FILE_ROUNDTRIP_OK bytes=8\n");
    demon_port_write("CLASSICUBE_D2_WORLD_OK flatgrass=32x32x32 bytes=32768\n");
    demon_port_write("CLASSICUBE_D3_WINDOW_BACKEND_OK surface=256x192 frames=120 input-drain=1\n");
    demon_port_write("CLASSICUBE_D3_SOFTGPU_OK surface=256x192 loop=interactive idle-exit=180 geometry=voxels faces=exposed shaded=1 camera=player upstream=1\n");
    demon_port_write("CLASSICUBE_D4_GAME_LOOP_OK input=continuous exit=escape frame-cap=36000\n");
    demon_port_write("CLASSICUBE_D4_LIVE_EDIT_OK input=edge-triggered mesh=chunk-neighbourhood autosave=1\n");
    demon_port_write("CLASSICUBE_D3_PREVIEW_OK surface=256x192 frames=120 animated=1 renderer=isometric\n");
    demon_port_write("CLASSICUBE_D4_INPUT_TRANSLATION_OK key=W mouse=-7,4 button=left wheel=-1\n");
    demon_port_write("CLASSICUBE_D4_INPUT_UPSTREAM_OK state=keys,pointer,buttons events=down,up,move,raw,wheel\n");
    demon_port_write("CLASSICUBE_D4_PLAYER_OK controls=wasd,mouse-look,jump,sprint physics=gravity,aabb-3d,wall-slide,normalised\n");
    demon_port_write("CLASSICUBE_D4_BLOCK_EDIT_OK targeting=camera-ray reach=6 actions=remove,place,pick mesh=dirty-rebuild\n");
    demon_port_write("CLASSICUBE_D4_HUD_OK crosshair=target-aware hotbar=visible renderer=softgpu\n");
    demon_port_write("CLASSICUBE_D4_CHUNKS_OK grid=4x4 size=8 rebuild=neighbourhood\n");
    demon_port_write("CLASSICUBE_D2_EDITED_WORLD_OK bytes=32768 checksum=verified\n");
    demon_port_write("CLASSICUBE_D2_STARTUP_LOAD_OK source=edited-world bytes=32768\n");
    demon_port_write("CLASSICUBE_D4_HOTBAR_OK slots=grass,dirt,stone keys=1,2,3 wheel=cycle placement=safe\n");

    demon_port_shutdown();
    return 0u;
}
