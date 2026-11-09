/*
 * VideoProcessor source plugin interface.
 *
 * A source plugin is a DLL in plugins/sources/ beside VideoProcessor.exe.
 * VP shows it as one more capture input. Without a plugin, VP behaves
 * exactly as before.
 *
 * Plain C with fixed-width fields so a plugin built with any compiler
 * (MinGW, MSVC, later GCC for a Linux .so) agrees with VP on the layout.
 * No C++ types, exceptions, CRT objects or allocations cross this boundary:
 * memory is freed by whoever allocated it.
 *
 * Entry point: the plugin exports one function, vp_source_plugin_get_api,
 * which returns its function table. VP asks for VP_SOURCE_PLUGIN_ABI_VERSION
 * first, then for each older version down to
 * VP_SOURCE_PLUGIN_ABI_VERSION_OLDEST, so a plugin built for an older
 * version keeps loading without a rebuild. VP refuses the plugin when the
 * table's abi_version differs from the version it answered, or its
 * struct_size is smaller than the table VP was built with. A newer version
 * only adds to this interface; it never changes an older version's
 * structures.
 *
 * Threads (VP keeps all of its own threading; the plugin never calls VP
 * except through vp_source_host):
 *   - create, start, stop, get_state, send_command: VP's GUI thread.
 *   - destroy: any thread, once, after every frame is released (the last
 *     release can come from a renderer thread after stop).
 *   - wait_frame: VP's one reader thread for this instance, between start
 *     and stop only.
 *   - release_frame, clock_now, frames_*: any thread, at any time between
 *     create and destroy. VP may hold several frames at once and release
 *     them in any order, after stop has returned.
 *   - host->log: the plugin may call it from any thread.
 *   - host->readback, wait_presented, select_profile, get_profile (version
 *     6): the plugin's own threads only, never VP's GUI thread or its
 *     reader (never from send_command or wait_frame): each blocks until VP
 *     has done it, and VP's GUI thread or reader is what does it.
 *
 * Timing: the plugin is the input's clock, as a capture card is. Every
 * frame timestamp is on that clock, in clock_ticks_per_second ticks.
 *
 * Frames: one block of tightly packed planes, no row padding (R10L only
 * pads its rows, as below).
 *   P010: Y plane (width * 2 bytes x height rows), then interleaved CbCr
 *         (width * 2 bytes x height / 2 rows). 10 bits in the high bits of
 *         each little-endian 16-bit word; low 6 bits zero.
 *   R10L (version 3): one plane, one little-endian 32-bit word per pixel,
 *         R in bits 31-22, G in 21-12, B in 11-2, bits 1-0 zero; RGB 4:4:4
 *         at limited range (64-940). Each row is VP's R10l row,
 *         ((width + 63) / 64) * 256 bytes: padded to a multiple of 64
 *         pixels, padding ignored. An 8-bit source may be carried by an
 *         exact shift (value << 2), declared with bit_depth 8.
 *   BGRA8: one plane, bytes B, G, R, A (width * 4 bytes x height rows),
 *         delivered exactly as an HDMI RGB capture would deliver it. It is
 *         how a plugin sends a TV-led Dolby Vision picture: declare it SDR,
 *         Rec.709, and VP's Dolby Vision tunnel setting decides, as for a
 *         capture card.
 *   P010 is limited range. A plugin must not declare a frame it cannot
 *   deliver in this layout. An 8-bit source may be carried as P010 by an
 *   exact shift (value << 8, low 8 bits zero), declared with bit_depth 8;
 *   nothing else is converted (no chroma resampling, no range change).
 *
 * Reserved fields must be zero.
 *
 * Cues: a frame may carry a subtitle image and a message image, never drawn
 * into its picture; VP draws them over its output. Each is premultiplied
 * BGRA (bytes B, G, R, A), sRGB with white at 1.0; VP chooses the level
 * white is shown at. A cue's id changes only when its image does, so VP
 * copies a cue only when the id is new.
 */

#ifndef VP_SOURCE_PLUGIN_H
#define VP_SOURCE_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VP_SOURCE_PLUGIN_ABI_VERSION        6u
#define VP_SOURCE_PLUGIN_ABI_VERSION_OLDEST 2u   /* oldest VP still loads */

#if defined(_WIN32)
#define VP_SOURCE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define VP_SOURCE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Results. Every function that can fail returns one; negative is failure. */
#define VP_SOURCE_OK                0
#define VP_SOURCE_ERROR            -1   /* details went to host->log */
#define VP_SOURCE_ERROR_ARGUMENT   -2
#define VP_SOURCE_ERROR_STATE      -3   /* e.g. wait_frame before start */
#define VP_SOURCE_ERROR_UNSUPPORTED -4
#define VP_SOURCE_ERROR_TIMEOUT     -5  /* version 6: a host call's wait ran
                                           out; nothing was done */

/* wait_frame outcomes (non-negative). */
#define VP_SOURCE_WAIT_FRAME          0   /* *frame filled; release it later */
#define VP_SOURCE_WAIT_STATE_CHANGED  1   /* call get_state before the next
                                             wait; no frame was returned */
#define VP_SOURCE_WAIT_TIMEOUT        2
#define VP_SOURCE_WAIT_STOPPED        3   /* stop was called; return now */

/* Frame encodings a plugin may declare. Only plugins use these: VP's
 * capture cards never deliver them. */
#define VP_SOURCE_ENCODING_NONE   0u
#define VP_SOURCE_ENCODING_P010   1u
/* 2 is not used. */
#define VP_SOURCE_ENCODING_BGRA8  3u
#define VP_SOURCE_ENCODING_R10L   4u   /* version 3 */

/* Transfer functions and colour spaces. Our own numbers, not VP's enum
 * values: the adapter maps each one by name, so VP can reorder its enums
 * without breaking a plugin. */
#define VP_SOURCE_EOTF_UNKNOWN    0u
#define VP_SOURCE_EOTF_SDR        1u
#define VP_SOURCE_EOTF_PQ         2u
#define VP_SOURCE_EOTF_HLG        3u

#define VP_SOURCE_COLORSPACE_UNKNOWN     0u
#define VP_SOURCE_COLORSPACE_REC_601_525 1u
#define VP_SOURCE_COLORSPACE_REC_601_625 2u
#define VP_SOURCE_COLORSPACE_REC_709     3u
#define VP_SOURCE_COLORSPACE_BT_2020     4u

/* vp_source_frame.flags */
#define VP_SOURCE_FRAME_DISCONTINUITY   (1u << 0)   /* first frame after a
                                                       seek, file change or
                                                       format change */
#define VP_SOURCE_FRAME_CADENCE_RESTART (1u << 1)   /* the source's timing
                                                       has settled here, e.g.
                                                       after a seek or its
                                                       start-up correction;
                                                       VP restarts its cadence
                                                       estimate. Independent
                                                       of DISCONTINUITY */

/* vp_source_state.flags (version 4) */
#define VP_SOURCE_STATE_EXACT_SIGNAL    (1u << 0)   /* the plugin generates
                                                       this signal and states
                                                       it exactly (a pattern
                                                       generator): VP takes
                                                       it as declared, with
                                                       no LLDV inference and
                                                       no Dolby Vision
                                                       tunnel. A player of
                                                       decoded files leaves
                                                       it clear */
#define VP_SOURCE_STATE_RAW_OUTPUT      (1u << 1)   /* version 5, with
                                                       EXACT_SIGNAL only: VP
                                                       writes the frame's
                                                       codes to its output
                                                       unprocessed (no
                                                       libplacebo pass, no
                                                       overlays), or black
                                                       when it cannot (R10L
                                                       at the output's size,
                                                       limited range) */
#define VP_SOURCE_STATE_NATIVE_RASTER   (1u << 2)   /* version 6: show the
                                                       frames 1:1, pixel for
                                                       pixel: VP sets its
                                                       Screen and Zoom
                                                       profiles aside (no
                                                       resize, no crop, no
                                                       zoom) while it is set;
                                                       nothing is saved */

/* Log levels for host->log. */
#define VP_SOURCE_LOG_ERROR   0u
#define VP_SOURCE_LOG_WARNING 1u
#define VP_SOURCE_LOG_INFO    2u
#define VP_SOURCE_LOG_DEBUG   3u

/* HDR10 static metadata, in VP's units: CIE 1931 xy chromaticities and
 * cd/m2. has_mastering covers primaries, white point and both mastering
 * luminances; has_light_level covers MaxCLL and MaxFALL. */
typedef struct vp_source_hdr10 {
    uint32_t has_mastering;
    uint32_t has_light_level;
    double   red_x, red_y;
    double   green_x, green_y;
    double   blue_x, blue_y;
    double   white_x, white_y;
    double   max_mastering_luminance;
    double   min_mastering_luminance;
    double   max_cll;
    double   max_fall;
} vp_source_hdr10;

/* The decoder's own colour tags, untouched (libavutil AVCOL_* numbers).
 * VP does not render from these; it logs them, and they let VP check what
 * the plugin mapped into eotf and colorspace. set == 0: none known. */
typedef struct vp_source_raw_tags {
    uint32_t set;
    int32_t  primaries;
    int32_t  transfer;
    int32_t  matrix;
    int32_t  range;
    int32_t  chroma_location;
    uint32_t sar_num;           /* sample aspect ratio; 0/0 if unknown */
    uint32_t sar_den;
} vp_source_raw_tags;

/* The signal the input is delivering now: VP's VideoState and DisplayMode
 * in plain fields. valid == 0 means no signal; ignore the rest. */
typedef struct vp_source_state {
    uint32_t struct_size;       /* sizeof(vp_source_state), set by VP */
    uint32_t valid;
    uint32_t width;             /* pixels */
    uint32_t height;
    uint32_t time_scale;        /* ticks per second (DisplayMode) */
    uint32_t frame_duration;    /* ticks per frame, e.g. 1001 at 24000 */
    uint32_t encoding;          /* VP_SOURCE_ENCODING_* */
    uint32_t eotf;              /* VP_SOURCE_EOTF_* */
    uint32_t colorspace;        /* VP_SOURCE_COLORSPACE_* */
    uint32_t bit_depth;         /* the source's bits per sample: 8 or 10 (P010
                                   and R10L only); 0 = the encoding's own
                                   depth */
    vp_source_hdr10    hdr10;
    vp_source_raw_tags raw;
    uint32_t flags;             /* VP_SOURCE_STATE_* (version 4; RAW_OUTPUT
                                   version 5; NATIVE_RASTER version 6) */
} vp_source_state;

/* One cue image on a frame (see Cues at the top). id == 0: none; ignore
 * the rest. x and width lie inside the frame. y may run up to a frame's
 * height above or below it (a subtitle in a film's bars): -H <= y and
 * y + height <= 2H, where H is the frame height. VP draws such rows in
 * the bars around the picture. */
typedef struct vp_source_cue {
    uint64_t    id;             /* unique to one image; repeats while it is
                                   unchanged */
    int32_t     x;              /* bounds in the frame's pixels */
    int32_t     y;
    uint32_t    width;
    uint32_t    height;
    uint32_t    stride;         /* bytes per row, at least width * 4 */
    uint32_t    reserved0;
    const void* pixels;         /* valid while the frame is */
} vp_source_cue;

/* One frame lent to VP. VP reads data until it calls release_frame with
 * the same handle; the plugin keeps the memory valid until then. */
typedef struct vp_source_frame {
    const void* data;           /* planes as described at the top */
    uint64_t    size;           /* bytes; must equal the state's frame size */
    uint64_t    counter;        /* monotonic; a gap means frames were missed */
    int64_t     timestamp;      /* plugin clock ticks: when this frame is
                                   due on screen */
    uint32_t    flags;          /* VP_SOURCE_FRAME_* */
    uint32_t    reserved0;
    void*       handle;         /* opaque, for release_frame */
    vp_source_cue subtitle;     /* the subtitle track's image */
    vp_source_cue message;      /* a short on-screen message */
} vp_source_frame;

/* A rectangle of VP's output, in output pixels, top-left origin (version 6). */
typedef struct vp_source_rect {
    uint32_t x, y;
    uint32_t width, height;
} vp_source_rect;

/* host->readback flags (version 6). */
#define VP_SOURCE_READBACK_PNG  (1u << 0)   /* also write VP's whole-frame
                                               capture of the same frame (as
                                               Ctrl+Alt+S does); its path is
                                               returned */

/* What host->readback read: VP's output as it was about to be presented,
 * after every stage of VP's processing, before the display. */
typedef struct vp_source_readback {
    uint32_t struct_size;       /* set by the plugin */
    uint32_t bits;              /* the output's code depth: 10 (only 10-bit
                                   R10G10B10A2 output can be read) */
    uint64_t source_frame;      /* the frame counter read (at least the one
                                   asked for) */
    uint32_t output_width;      /* the whole output, pixels */
    uint32_t output_height;
    uint32_t min[3];            /* per channel R, G, B, over the rectangle */
    uint32_t max[3];
    double   mean[3];
    uint64_t hash;              /* FNV-1a 64 of the rectangle's pixels as
                                   32-bit words, row by row */
    uint64_t vp_present;        /* VP's present count for the present it
                                   rode; 0 if that present failed */
    uint32_t dxgi_present_id;   /* DXGI's present count after it */
    uint32_t reserved0;
    char     png_path[520];     /* UTF-8; READBACK_PNG only, else empty:
                                   the file VP is writing (it may still be
                                   being written when readback returns) */
} vp_source_readback;

/* What host->wait_presented saw: a frame handed to the display. */
typedef struct vp_source_presented {
    uint32_t struct_size;       /* set by the plugin */
    uint32_t dxgi_stats;        /* 1: the DXGI fields below are valid */
    uint64_t source_frame;      /* the frame counter presented (at least the
                                   one asked for) */
    uint64_t vp_present;        /* VP's present count */
    int64_t  submit_qpc;        /* QueryPerformanceCounter at Present */
    uint32_t dxgi_present_id;   /* DXGI's present count after Present */
    uint32_t dxgi_shown_id;     /* DXGI's last present on the screen then */
    int64_t  dxgi_shown_qpc;    /* and when it went on the screen */
} vp_source_presented;

/* Services VP offers the plugin. Valid from create until destroy returns.
 * The version 6 functions are there only when abi_version >= 6 and
 * struct_size covers them; each answers VP_SOURCE_ERROR_UNSUPPORTED when
 * VP's renderer cannot do it. See Threads at the top. */
typedef struct vp_source_host {
    uint32_t abi_version;       /* the version this plugin answered */
    uint32_t struct_size;
    void*    context;           /* pass back unchanged */
    const char* plugin_dir;     /* UTF-8; the folder holding the plugin DLL */
    void (*log)(void* context, uint32_t level, const char* message_utf8);

    /* Version 6. Waits until VP has rendered a frame of this input whose
     * counter is source_frame or later, reads rect of its output and
     * returns its statistics in *out. A rect outside the output:
     * VP_SOURCE_ERROR_ARGUMENT. One readback at a time per VP:
     * VP_SOURCE_ERROR_STATE while another waits. */
    int32_t (*readback)(void* context, uint64_t source_frame,
                        const vp_source_rect* rect, uint32_t flags,
                        uint32_t timeout_ms, vp_source_readback* out);
    /* Version 6. Waits until VP has presented a frame of this input whose
     * counter is source_frame or later, in a present made after this call
     * began, and returns that present in *out. */
    int32_t (*wait_presented)(void* context, uint64_t source_frame,
                              uint32_t timeout_ms, vp_source_presented* out);
    /* Version 6. Selects profile in profile group group (as named in VP's
     * configuration), as a keypress would, but never saved; VP applies it
     * before returning. profile NULL releases every pick this plugin made:
     * each group goes back to exactly what it had before the first one. */
    int32_t (*select_profile)(void* context, const char* group_utf8,
                              const char* profile_utf8);
    /* Version 6. The profiles in force in group, comma separated, into
     * buffer (size bytes, NUL included); VP_SOURCE_ERROR_ARGUMENT when the
     * group is unknown or the buffer too small. */
    int32_t (*get_profile)(void* context, const char* group_utf8,
                           char* buffer, uint32_t size);
} vp_source_host;

typedef struct vp_source_instance vp_source_instance;

/* The plugin's function table. Static storage in the plugin. */
typedef struct vp_source_plugin_api {
    uint32_t abi_version;       /* VP_SOURCE_PLUGIN_ABI_VERSION */
    uint32_t struct_size;       /* sizeof(vp_source_plugin_api) */
    const char* name;           /* UTF-8; the input's name in VP */

    int32_t (*create)(const vp_source_host* host, vp_source_instance** out);
    void    (*destroy)(vp_source_instance* inst);   /* any thread, after
                                                       every frame is
                                                       released */

    int32_t (*start)(vp_source_instance* inst);
    int32_t (*stop)(vp_source_instance* inst);      /* wakes wait_frame */

    int32_t (*get_state)(vp_source_instance* inst, vp_source_state* out);

    /* Waits up to timeout_ms. Returns VP_SOURCE_WAIT_* or a negative
     * result. The first call after start returns STATE_CHANGED. */
    int32_t (*wait_frame)(vp_source_instance* inst, uint32_t timeout_ms,
                          vp_source_frame* out);
    void    (*release_frame)(vp_source_instance* inst, void* handle);

    int64_t (*clock_now)(vp_source_instance* inst);
    int64_t (*clock_ticks_per_second)(vp_source_instance* inst);

    uint64_t (*frames_captured)(vp_source_instance* inst);
    uint64_t (*frames_missed)(vp_source_instance* inst);

    /* Player control: one UTF-8 command line, e.g. "pause", "seek +10".
     * VP_SOURCE_ERROR_UNSUPPORTED if the plugin does not know it. */
    int32_t (*send_command)(vp_source_instance* inst,
                            const char* command_utf8);
} vp_source_plugin_api;

/* The one exported symbol. Returns NULL if the plugin cannot run with
 * this host (it may log why). */
typedef const vp_source_plugin_api* (*vp_source_plugin_get_api_fn)(
    uint32_t host_abi_version);

#define VP_SOURCE_PLUGIN_ENTRY_NAME "vp_source_plugin_get_api"

#ifdef __cplusplus
}
#endif

#endif
