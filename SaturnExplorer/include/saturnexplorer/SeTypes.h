/* Saturn Explorer — POD data types crossing the seams.
 *
 * Plain C. Enum values that mirror Saturn hardware fields are annotated with
 * their source (Docs/Saturn/VDP1.pdf, VDP2.pdf). Where an enum mirrors a raw
 * register field, its numeric values match the hardware encoding so a driver
 * or the core can cast between them.
 */
#ifndef SATURNEXPLORER_SE_TYPES_H
#define SATURNEXPLORER_SE_TYPES_H

#include "SeAbi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 *  VDP1 hardware enums (VDP1.pdf ch.6 CMDCTRL / CMDPMOD)
 * ------------------------------------------------------------------ */

/* CMDCTRL command code (low nibble). Values match the hardware encoding. */
typedef enum se_command_type {
    SE_CMD_NORMAL_SPRITE   = 0x0,
    SE_CMD_SCALED_SPRITE   = 0x1,
    SE_CMD_DISTORTED_SPRITE= 0x2,
    SE_CMD_POLYGON         = 0x4,
    SE_CMD_POLYLINE        = 0x5,
    SE_CMD_LINE            = 0x6,
    SE_CMD_USER_CLIP       = 0x8,   /* control: set user clipping coordinates */
    SE_CMD_SYSTEM_CLIP     = 0x9,   /* control: set system clipping coordinates */
    SE_CMD_LOCAL_COORD     = 0xA,   /* control: set local coordinate origin */
    SE_CMD_UNKNOWN         = 0xF
} se_command_type;

/* How the command table walk treats this entry: CMDCTRL "JP" jump-select +
 * end bit. Determines whether the sprite is drawn, skipped, or ends the list. */
typedef enum se_command_status {
    SE_CMDSTAT_NORMAL = 0,  /* draw, then link to next */
    SE_CMDSTAT_SKIP   = 1,  /* skip drawing, still link */
    SE_CMDSTAT_END    = 2   /* end of command list */
} se_command_status;

/* CMDPMOD color mode field (3 bits). Values match the hardware encoding. */
typedef enum se_color_mode {
    SE_COLOR_BANK_16   = 0,  /* 4 bpp, color bank, 16 colors */
    SE_COLOR_LUT_16    = 1,  /* 4 bpp, color lookup table (CLUT), 16 colors */
    SE_COLOR_BANK_64   = 2,  /* 8 bpp, color bank, 64 colors */
    SE_COLOR_BANK_128  = 3,  /* 8 bpp, color bank, 128 colors */
    SE_COLOR_BANK_256  = 4,  /* 8 bpp, color bank, 256 colors */
    SE_COLOR_RGB555    = 5   /* 16 bpp, RGB code, 32,768 colors */
} se_color_mode;

/* CMDPMOD color-calculation field (3 bits): how the pixel combines with the
 * frame buffer. Values match the hardware encoding. */
typedef enum se_color_calc {
    SE_CC_REPLACE            = 0,
    SE_CC_SHADOW             = 1,
    SE_CC_HALF_LUMINANCE     = 2,
    SE_CC_HALF_TRANSPARENT   = 3,
    SE_CC_GOURAUD            = 4,
    SE_CC_GOURAUD_HALF_LUM   = 6,
    SE_CC_GOURAUD_HALF_TRANS = 7
} se_color_calc;

/* Transparency handling, derived from CMDPMOD SPD (transparent-pixel disable)
 * plus the color mode's transparency semantics. */
typedef enum se_transparency_mode {
    SE_TRANSP_NONE      = 0,  /* SPD set: code 0 is drawn as opaque */
    SE_TRANSP_PER_PIXEL = 1   /* code 0 (or MSB=0 in RGB) is transparent */
} se_transparency_mode;

/* Coarse "draw mode" as surfaced in the Selected Object panel. */
typedef enum se_draw_mode {
    SE_DRAW_NORMAL    = 0,
    SE_DRAW_MESH      = 1,
    SE_DRAW_SHADOW    = 2,
    SE_DRAW_HALF_LUM  = 3,
    SE_DRAW_HALF_TRANS= 4
} se_draw_mode;

/* ------------------------------------------------------------------ *
 *  VDP2 hardware enums (VDP2.pdf §3.4 Color RAM Mode; scroll screens)
 * ------------------------------------------------------------------ */

typedef enum se_vdp2_layer {
    SE_LAYER_NBG0 = 0,   /* Scroll A */
    SE_LAYER_NBG1 = 1,   /* Scroll B */
    SE_LAYER_NBG2 = 2,   /* Scroll C */
    SE_LAYER_NBG3 = 3,   /* Scroll D */
    SE_LAYER_RBG0 = 4,   /* Rotation */
    SE_LAYER_COUNT = 5
} se_vdp2_layer;

typedef enum se_cram_mode {
    SE_CRAM_RGB555_1024 = 0,  /* mode 0: RGB 5-bit each, 1024 colors */
    SE_CRAM_RGB555_2048 = 1,  /* mode 1: RGB 5-bit each, 2048 colors */
    SE_CRAM_RGB888_1024 = 2   /* mode 2: RGB 8-bit each, 1024 colors */
} se_cram_mode;

/* ------------------------------------------------------------------ *
 *  Geometry & imaging primitives
 * ------------------------------------------------------------------ */

typedef struct se_vec2 { float x, y; } se_vec2;
typedef struct se_vec3 { float x, y, z; } se_vec3;

typedef enum se_pixel_format {
    SE_PIXFMT_RGBA8888 = 0    /* 4 bytes/pixel, R,G,B,A byte order */
} se_pixel_format;

/* A decoded image. Buffer is CALLER-allocated: pass pixels + capacity, and the
 * core fills width/height/stride. Call once with pixels == NULL to learn the
 * required byte size (returned in *out_size via the owning function). */
typedef struct se_image {
    uint32_t        width;
    uint32_t        height;
    uint32_t        stride;     /* bytes per row */
    se_pixel_format format;
    uint8_t*        pixels;     /* caller-owned */
    size_t          capacity;   /* bytes available at 'pixels' */
} se_image;

/* Reference to a texture as it lives in VDP1 VRAM. */
typedef struct se_texture_ref {
    uint32_t      vram_address; /* byte address in VDP1 VRAM */
    uint16_t      width;        /* pixels */
    uint16_t      height;       /* pixels */
    se_color_mode color_mode;
    uint32_t      clut_address; /* for SE_COLOR_LUT_16; else 0 */
    uint16_t      palette_bank; /* for bank modes */
} se_texture_ref;

typedef struct se_palette_entry {
    uint8_t r, g, b, a;         /* expanded to 8-bit for display */
    uint16_t raw;               /* original RGB555 / packed value */
} se_palette_entry;

typedef struct se_palette {
    uint32_t         clut_address;
    se_cram_mode     mode;
    uint16_t         count;         /* number of valid entries below */
    se_palette_entry entries[256];  /* CLUT/sub-palette view */
} se_palette;

/* ------------------------------------------------------------------ *
 *  Command / sprite records
 * ------------------------------------------------------------------ */

/* One VDP1 command as parsed from the command table. Mirrors the fields shown
 * in the Selected Object panel. */
typedef struct se_command {
    uint32_t          index;           /* command # in the list */
    uint32_t          table_address;   /* address of this command table (CMDCTRL) */
    uint32_t          link_address;    /* CMDLINK: next command table */
    se_command_type   type;
    se_command_status status;

    uint32_t          texture_address; /* CMDSRCA * 8 */
    uint32_t          clut_address;    /* CMDCOLR * 8 in LUT mode */
    uint16_t          palette_bank;
    uint32_t          gouraud_table;   /* CMDGRDA * 8 */

    uint16_t          width;           /* pixels (CMDSIZE) */
    uint16_t          height;          /* pixels */
    int16_t           x;               /* primary position (vertex A / CMDXA) */
    int16_t           y;               /* CMDYA */

    float             scale_x;
    float             scale_y;
    float             rotation_deg;    /* derived from the 4 corners */

    uint8_t           flip_x;          /* 0/1 */
    uint8_t           flip_y;          /* 0/1 */
    uint8_t           priority;        /* 0..7, resolved via VDP2 priority regs */
    uint8_t           gouraud;         /* 0/1 */
    uint8_t           color_calc;      /* 0/1 (enabled) */

    se_color_mode        color_mode;
    se_draw_mode         draw_mode;
    se_transparency_mode transparency;
    se_color_calc        color_calc_mode;

    uint16_t          raw_cmdctrl;     /* raw register words, for the low-level view */
    uint16_t          raw_cmdpmod;

    char              description[64]; /* optional label ("Sakura (Body)"); may be empty */
} se_command;

/* Same sprite in 2D screen space: the four corners exactly where the Saturn
 * places them. Rasterizing these in priority order reproduces the frame; the
 * host hit-tests clicks against these quads. See ARCHITECTURE.md §7. */
typedef struct se_sprite_2d {
    uint32_t             command_index;
    uint32_t             object_number;
    se_vec2              corners[4];   /* A,B,C,D screen-space */
    se_vec2              uv[4];        /* texture coordinates */
    uint8_t              priority;
    uint8_t              flip_x, flip_y, gouraud;
    se_color_mode        color_mode;
    se_transparency_mode transparency;
    se_draw_mode         draw_mode;
    se_texture_ref       texture;
} se_sprite_2d;

/* Same sprite in 3D world space: corners separated along Z by priority + draw
 * order so overlapping layers pull apart. The driver owns the camera. */
typedef struct se_sprite_3d {
    uint32_t             command_index;
    uint32_t             object_number;
    se_vec3              corners[4];   /* A,B,C,D world-space */
    se_vec2              uv[4];
    se_texture_ref       texture;      /* self-contained material, so a GPU host */
    se_transparency_mode transparency; /* can render without the 2D sprite list */
} se_sprite_3d;

/* ------------------------------------------------------------------ *
 *  VRAM map
 * ------------------------------------------------------------------ */

typedef enum se_vram_region_kind {
    SE_VRAM_UNUSED     = 0,
    SE_VRAM_TEXTURE    = 1,
    SE_VRAM_CLUT       = 2,
    SE_VRAM_CMD_TABLE  = 3,
    SE_VRAM_GOURAUD    = 4,
    SE_VRAM_OTHER      = 5
} se_vram_region_kind;

typedef struct se_vram_region {
    uint32_t            address;   /* start byte address in VDP1 VRAM */
    uint32_t            size;      /* bytes */
    se_vram_region_kind kind;
    uint32_t            ref_index; /* associated command index, or 0xFFFFFFFF */
} se_vram_region;

/* ------------------------------------------------------------------ *
 *  Render options (all the Layer Controls toggles from the mockup)
 * ------------------------------------------------------------------ */

typedef struct se_render_opts {
    /* VDP1 */
    uint8_t show_vdp1_sprites;
    uint8_t show_wireframe;
    uint8_t show_bounding_boxes;
    uint8_t show_object_numbers;
    /* VDP2 backgrounds — index by se_vdp2_layer */
    uint8_t show_layer[SE_LAYER_COUNT];
    uint8_t show_window;
    uint8_t show_color_calculation;
    uint8_t show_shadow_highlight;
    /* selection highlight */
    int32_t highlight_command;   /* command index to outline, or -1 */
} se_render_opts;

/* ------------------------------------------------------------------ *
 *  3D world-view camera (host-owned; the core software-renders with it)
 * ------------------------------------------------------------------ */

typedef struct se_camera3d {
    float    yaw;             /* radians, orbit around world Y */
    float    pitch;          /* radians, orbit around world X */
    float    distance;       /* camera distance from the target */
    float    fov;            /* perspective scale, in pixels */
    uint32_t viewport_width;  /* output image size */
    uint32_t viewport_height;
} se_camera3d;

/* ------------------------------------------------------------------ *
 *  Memory history (the load chain: File -> CD Read -> DMA -> Write)
 * ------------------------------------------------------------------ */

typedef enum se_mem_event_kind {
    SE_MEM_CPU_WRITE = 0,
    SE_MEM_DMA       = 1,
    SE_MEM_COPY      = 2,
    SE_MEM_READ      = 3,   /* e.g. CD read into work RAM */
    SE_MEM_FILE_LOAD = 4
} se_mem_event_kind;

typedef struct se_mem_event {
    se_mem_event_kind kind;
    uint64_t frame;
    uint32_t src_addr;      /* 0 if N/A */
    uint32_t dst_addr;
    uint32_t size;
    uint32_t channel;       /* DMA channel, or driver-defined tag */
    char     detail[96];    /* e.g. "DATA\\FACE03.BIN (offset 0x1A000)" */
} se_mem_event;

/* ------------------------------------------------------------------ *
 *  ROM / archive search
 * ------------------------------------------------------------------ */

typedef enum se_search_mode {
    SE_SEARCH_ROM      = 0,  /* raw ISO scan */
    SE_SEARCH_FILES    = 1,  /* per-file within the filesystem */
    SE_SEARCH_LZSS     = 2,  /* LZSS-compressed assets */
    SE_SEARCH_ARCHIVES = 3,  /* known archive formats */
    SE_SEARCH_RAW      = 4   /* raw binary pattern */
} se_search_mode;

typedef struct se_search_query {
    se_search_mode mode;
    se_texture_ref target;   /* the asset to trace (texture being searched for) */
    uint8_t        match_palette; /* also require the CLUT to match */
} se_search_query;

typedef struct se_search_result {
    char     archive[64];    /* archive/container name, or empty */
    char     file[128];      /* file path, e.g. "DISC_1\\DATA\\FACE03.BIN" */
    uint64_t offset;
    uint64_t size;
    char     asset_type[16]; /* "BIN", "TIM", ... */
    uint8_t  confidence;     /* 0..100 */
} se_search_result;

/* ------------------------------------------------------------------ *
 *  Reference explorer ("what uses this texture / palette?")
 * ------------------------------------------------------------------ */

typedef struct se_reference {
    uint32_t command_index;
    uint32_t object_number;
    int16_t  x, y;
    uint16_t width, height;
} se_reference;

/* ------------------------------------------------------------------ *
 *  System status (status bar) & config
 * ------------------------------------------------------------------ */

typedef struct se_system_status {
    uint64_t frame;
    uint32_t sh2_master_pc;
    uint32_t sh2_slave_pc;
    uint16_t scanline;
    uint8_t  hblank;
    uint8_t  vblank;
    uint8_t  vdp1_busy;
    uint8_t  vdp2_busy;
} se_system_status;

typedef struct se_config {
    uint32_t abi_version;    /* set to SE_ABI_VERSION by the host */
    uint32_t reserved;
} se_config;

typedef struct se_framebuffer {
    uint32_t width;
    uint32_t height;
    uint8_t* pixels;         /* caller-owned RGBA8888 */
    size_t   capacity;
} se_framebuffer;

typedef struct se_disc_info {
    char     game_title[64];
    char     region[8];      /* "NTSC-J", ... */
    uint64_t size_bytes;
    uint8_t  disc_count;
} se_disc_info;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_TYPES_H */
