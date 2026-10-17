/* Kai-Term — image decoding: the one place where bytes become pixels.
 *
 * Every picture Kai-Term shows arrives as bytes somewhere and leaves here as RGBA8, which is the only
 * pixel format the renderer knows (one GL texture per picture, composited by the shader as
 * c + bg*(1-c.a), so no GL blending is needed). Two producers share this file:
 *   - src/vt.c  — the kitty graphics protocol (an application transmits a picture inline);
 *   - src/fm.c  — the file manager's preview pane (a picture read from disk).
 * Keeping the decoders in one place is what makes "PNG works in both" true by construction instead of
 * by coincidence.
 *
 * Formats are recognized by their own bytes (img_sniff), not by a file name or by the f= a protocol
 * claims: a container is self-describing and an application may set f= loosely.
 *
 * Dependencies: libpng and zlib are already required by the graphics protocol. JPEG and WebP use the
 * system libraries (libjpeg, libwebp). GIF is decoded HERE, in-tree, because giflib is not a
 * dependency of this project and the format is small enough to own: a header, one color table and an
 * LZW stream.
 *
 * Nothing here premultiplies: the callers do it (img_premultiply) at the moment they know where the
 * pixels are going, exactly as the graphics path always has.
 */
#include "kai-term.h"

#include <setjmp.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

/* The caps live in kai-term.h (IMG_DIM_MAX / IMG_PIX_MAX): the graphics protocol and the file
   manager's previews must refuse the same picture, and two copies of a limit is how one of them
   drifts. */

void img_premultiply(uint8_t *rgba, long long npix)
{
    for (long long i = 0; i < npix; i++) {
        unsigned a = rgba[i * 4 + 3];
        for (int k = 0; k < 3; k++)
            rgba[i * 4 + k] = (uint8_t)((rgba[i * 4 + k] * a + 128) / 255);
    }
}

static int img_size_ok(int w, int h, const char *kind)
{
    if (w < 1 || h < 1 || w > IMG_DIM_MAX || h > IMG_DIM_MAX ||
        (long long)w * (long long)h > IMG_PIX_MAX) {
        klog(L_WARN, "image: %s %dx%d px is over the limit", kind, w, h);
        return 0;
    }
    return 1;
}

/* ---------- PNG (libpng simplified API) ---------- */

uint8_t *img_decode_png(const uint8_t *mem, size_t n, int *w, int *h)
{
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, mem, n)) {
        klog(L_WARN, "image: png: %s", img.message);
        return NULL;
    }
    if (!img_size_ok((int)img.width, (int)img.height, "png")) {
        png_image_free(&img);
        return NULL;
    }
    img.format = PNG_FORMAT_RGBA;
    size_t sz = PNG_IMAGE_SIZE(img);
    uint8_t *out = malloc(sz ? sz : 1);
    if (!out) {
        png_image_free(&img);
        return NULL;
    }
    if (!png_image_finish_read(&img, NULL, out, 0, NULL)) {
        klog(L_WARN, "image: png read: %s", img.message);
        png_image_free(&img);
        free(out);
        return NULL;
    }
    *w = (int)img.width;
    *h = (int)img.height;
    return out;
}

/* ---------- JPEG (libjpeg) ---------- */

/* libjpeg's default error manager prints and then calls exit(). Neither is acceptable in a terminal:
   a corrupt thumbnail must not delete the user's session, and its stderr noise would land in the
   shell. So warnings are swallowed and the fatal path unwinds back into decode_jpeg. */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
} JErr;

/* libjpeg calls emit_message for every warning and trace message; msg_level is unused here because
   none of them is worth a log line (a corrupt file produces a whole list of them). */
static void jerr_emit(j_common_ptr c, int msg_level)
{
    (void)c;
    (void)msg_level;
}

static void jerr_exit(j_common_ptr c)
{
    longjmp(((JErr *)c->err)->jb, 1);
}

static uint8_t *img_decode_jpeg(const uint8_t *mem, size_t n, int *w, int *h)
{
    struct jpeg_decompress_struct ci;
    JErr je;
    uint8_t *out = NULL, *row = NULL;

    memset(&ci, 0, sizeof ci);
    ci.err = jpeg_std_error(&je.pub);
    je.pub.emit_message = jerr_emit;
    je.pub.error_exit = jerr_exit;
    if (setjmp(je.jb)) {
        /* The documented unwind: format the error libjpeg recorded, then tear the object down.
           A truncated or corrupt thumbnail must cost a preview, nothing else. */
        char msg[JMSG_LENGTH_MAX];
        (*je.pub.format_message)((j_common_ptr)&ci, msg);
        klog(L_WARN, "image: jpeg: %s", msg);
        jpeg_destroy_decompress(&ci);
        free(out);
        free(row);
        *w = *h = 0;
        return NULL;
    }
    jpeg_create_decompress(&ci);
    jpeg_mem_src(&ci, mem, (unsigned long)n);
    jpeg_read_header(&ci, TRUE);
    ci.out_color_space = JCS_RGB;      /* gray, CMYK and cmyk16 all end up as RGB */
    jpeg_start_decompress(&ci);
    int pw = (int)ci.output_width, ph = (int)ci.output_height;
    if (!img_size_ok(pw, ph, "jpeg")) {
        jpeg_destroy_decompress(&ci);
        return NULL;
    }
    out = malloc((size_t)pw * ph * 4);
    row = malloc((size_t)pw * 4);      /* 3 bytes per column, rounded up to 4 */
    if (!out || !row) {
        jpeg_destroy_decompress(&ci);
        free(out);
        free(row);
        return NULL;
    }
    int nch = ci.output_components;    /* 3 with JCS_RGB, but never assume */
    while (ci.output_scanline < ci.output_height) {
        /* jpeg_read_scanlines() BUMPS output_scanline, so the row index has to be captured BEFORE the
           read: using it afterwards wrote every row one line too low and ran past the end of `out`
           on the last one (found by the s71 probe: `free(): invalid next size`). */
        size_t y = (size_t)ci.output_scanline;
        JSAMPROW rp = row;
        if (jpeg_read_scanlines(&ci, &rp, 1) != 1)
            break;
        uint8_t *dst = out + y * (size_t)pw * 4;
        for (int x = 0; x < pw; x++) {
            dst[x * 4 + 0] = row[x * nch + 0];
            dst[x * 4 + 1] = nch > 1 ? row[x * nch + 1] : row[x * nch + 0];
            dst[x * 4 + 2] = nch > 2 ? row[x * nch + 2] : row[x * nch + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&ci);
    jpeg_destroy_decompress(&ci);
    free(row);
    *w = pw;
    *h = ph;
    return out;
}

/* ---------- WebP (libwebp) ---------- */

static uint8_t *img_decode_webp(const uint8_t *mem, size_t n, int *w, int *h)
{
    int pw = 0, ph = 0;
    if (!WebPGetInfo(mem, n, &pw, &ph)) {
        klog(L_WARN, "image: webp: not a decodable WebP stream");
        return NULL;
    }
    if (!img_size_ok(pw, ph, "webp"))
        return NULL;
    uint8_t *rgba = WebPDecodeRGBA(mem, n, &pw, &ph);
    if (!rgba) {
        klog(L_WARN, "image: webp: decode failed (%dx%d)", pw, ph);
        return NULL;
    }
    /* WebPDecodeRGBA hands over its own allocation; copy it into a plain malloc'd buffer so the
       caller can free() it without knowing which library produced the pixels. */
    uint8_t *out = malloc((size_t)pw * ph * 4);
    if (!out) {
        WebPFree(rgba);
        return NULL;
    }
    memcpy(out, rgba, (size_t)pw * ph * 4);
    WebPFree(rgba);
    *w = pw;
    *h = ph;
    return out;
}

/* ---------- GIF, decoded in-tree ----------
 * The whole format: a header, a logical screen descriptor, an optional global color table, then a
 * sequence of blocks — image descriptors (0x2C), extensions (0x21) and the trailer (0x3B). Data is
 * LZW-compressed with codes of at most 12 bits packed least-significant-bit first.
 * Only the FIRST image block is decoded: an animation's later frames are deltas over what was drawn
 * before, which a preview pane has no use for (documented in ISSUES.md). */

typedef struct {
    const uint8_t *p;
    size_t n, at;
} GifRd;

static unsigned gif_u16(GifRd *r)
{
    if (r->at + 2 > r->n)
        return 0xffff;
    unsigned v = (unsigned)r->p[r->at] | ((unsigned)r->p[r->at + 1] << 8);
    r->at += 2;
    return v;
}

/* Copy the whole sub-block chain that follows (length-prefixed, 0 ends it) into buf[0..cap),
   reporting the byte count in *len. Refuses to overflow the buffer: a malformed stream must not be
   able to write past what the caller reserved. */
static int gif_subblocks(GifRd *r, uint8_t *buf, size_t *len, size_t cap)
{
    for (;;) {
        if (r->at >= r->n)
            return 0;
        size_t sz = r->p[r->at++];
        if (sz == 0)
            return 1;
        if (r->at + sz > r->n)
            return 0;
        if (*len + sz > cap)
            return 0;
        memcpy(buf + *len, r->p + r->at, sz);
        *len += sz;
        r->at += sz;
    }
}

/* GIF's LZW. `minsz` is the code size the stream starts with (the clear code is 1<<minsz).
   Writes `want` palette indices; returns 1 when it produced all of them. */
static int gif_lzw(const uint8_t *d, size_t dn, int minsz, uint8_t *out, size_t want)
{
    if (minsz < 2 || minsz > 12)
        return 0;
    const int clear = 1 << minsz, end = clear + 1;
    uint16_t pref[4096];
    uint8_t suff[4096], first[4096];
    for (int c = 0; c <= end && c < 4096; c++)
        first[c] = (uint8_t)c;
    int csize = minsz + 1, next = end + 1, prev = -1;
    size_t o = 0;
    unsigned acc = 0;
    int nbits = 0;
    size_t i = 0;

    while (o < want) {
        while (nbits < csize) {
            if (i >= dn)
                return o == want;
            acc |= (unsigned)d[i++] << nbits;
            nbits += 8;
        }
        int code = (int)(acc & ((1u << csize) - 1u));
        acc >>= csize;
        nbits -= csize;

        if (code == clear) {
            csize = minsz + 1;
            next = end + 1;
            prev = -1;
            continue;
        }
        if (code == end)
            break;

        uint8_t stack[64];
        int sp = 0, base = -1;
        if (code < next) {
            int c = code;
            while (c > end) {
                if (sp >= (int)sizeof(stack))
                    return 0;
                stack[sp++] = suff[c];
                c = pref[c];
            }
            base = c;
            if (o < want)
                out[o++] = (uint8_t)c;
            while (sp > 0 && o < want)
                out[o++] = stack[--sp];
        } else if (code == next && prev >= 0) {
            /* KwKwK: the encoder references the entry it is about to define. */
            int c = prev;
            while (c > end) {
                if (sp >= (int)sizeof(stack))
                    return 0;
                stack[sp++] = suff[c];
                c = pref[c];
            }
            base = first[prev];
            if (o < want)
                out[o++] = (uint8_t)c;
            while (sp > 0 && o < want)
                out[o++] = stack[--sp];
            if (o < want)
                out[o++] = first[prev];
        } else {
            return 0;   /* a code the table has not defined and is not about to */
        }

        if (prev >= 0 && next < 4096) {
            pref[next] = (uint16_t)prev;
            suff[next] = (uint8_t)(base < 0 ? 0 : base);
            first[next] = first[prev];
            next++;
            if (next == (1 << csize) && csize < 12)
                csize++;
        }
        prev = code;
    }
    return o == want;
}

/* An interlaced GIF stores its rows in four passes: every 8th row from 0, then every 8th from 4,
   then every 4th from 2, then every 2nd from 1. This maps the y-th row IN THE STREAM to the row of
   the image it belongs to. The per-pass counts are ceil((ih - start) / step), which is what makes the
   boundaries exact (for ih 23 they are 3 + 3 + 6 + 11 = 23). */
static int gif_interlaced_row(int srow, int ih)
{
    int n0 = (ih + 7) / 8;   /* rows 0, 8, 16, ...  */
    int n1 = (ih + 3) / 8;   /* rows 4, 12, 20, ... */
    int n2 = (ih + 1) / 4;   /* rows 2, 6, 10, ...  */
    if (srow < n0)
        return srow * 8;
    srow -= n0;
    if (srow < n1)
        return srow * 8 + 4;
    srow -= n1;
    if (srow < n2)
        return srow * 4 + 2;
    srow -= n2;
    return srow * 2 + 1;
}

uint8_t *img_decode_gif(const uint8_t *mem, size_t n, int *w, int *h)
{
    GifRd r = { mem, n, 0 };
    if (n < 13 || memcmp(mem, "GIF8", 4) != 0 || (mem[4] != '7' && mem[4] != '9') || mem[5] != 'a') {
        klog(L_WARN, "image: not a GIF87a/GIF89a stream");
        return NULL;
    }
    r.at = 6;
    int sw = (int)gif_u16(&r), sh = (int)gif_u16(&r);
    if (r.at >= r.n)
        return NULL;
    unsigned packed = r.p[r.at++];
    r.at += 2;                       /* background index + pixel aspect ratio */
    if (!img_size_ok(sw, sh, "gif"))
        return NULL;
    uint8_t gct[3 * 256];
    int gct_n = 0;
    if (packed & 0x80) {
        gct_n = 2 << (packed & 7);
        if (r.at + (size_t)gct_n * 3 > r.n)
            return NULL;
        memcpy(gct, r.p + r.at, (size_t)gct_n * 3);
        r.at += (size_t)gct_n * 3;
    }

    int transparent = -1;
    uint8_t *out = calloc((size_t)sw * sh, 4);
    if (!out)
        return NULL;

    while (r.at < r.n) {
        unsigned block = r.p[r.at++];
        if (block == 0x3B)
            break;
        if (block == 0x21) {
            if (r.at >= r.n)
                break;
            unsigned label = r.p[r.at++];
            uint8_t buf[64];
            size_t len = 0;
            if (!gif_subblocks(&r, buf, &len, sizeof buf))
                break;
            /* Graphic Control Extension: packed bit 0 says the transparent index is meaningful.
               It precedes the image it applies to, so remembering it is all that is needed. */
            if (label == 0xF9 && len >= 4 && (buf[0] & 1))
                transparent = buf[3];
            continue;
        }
        if (block != 0x2C)
            break;                    /* unknown block: stop rather than guess */

        int ix = (int)gif_u16(&r), iy = (int)gif_u16(&r);
        int iw = (int)gif_u16(&r), ih = (int)gif_u16(&r);
        if (r.at >= r.n)
            break;
        unsigned ip = r.p[r.at++];
        uint8_t lct[3 * 256];
        const uint8_t *ct = gct;
        int ctn = gct_n;
        if (ip & 0x80) {
            ctn = 2 << (ip & 7);
            if (r.at + (size_t)ctn * 3 > r.n)
                break;
            memcpy(lct, r.p + r.at, (size_t)ctn * 3);
            r.at += (size_t)ctn * 3;
            ct = lct;
        }
        if (iw < 1 || ih < 1 || r.at >= r.n)
            break;
        int minsz = r.p[r.at++];
        uint8_t *data = malloc((size_t)iw * ih + 64);
        if (!data)
            break;
        size_t dn = 0;
        int okchain = gif_subblocks(&r, data, &dn, (size_t)iw * ih + 64);
        uint8_t *idx = malloc((size_t)iw * ih);
        if (!idx || !okchain || !gif_lzw(data, dn, minsz, idx, (size_t)iw * ih)) {
            free(data);
            free(idx);
            break;
        }
        free(data);

        size_t src = 0;
        for (int y = 0; y < ih; y++) {
            int dy = (ip & 0x40) ? gif_interlaced_row(y, ih) : y;
            int wy = iy + dy;
            if (wy < 0 || wy >= sh) {
                src += (size_t)iw;
                continue;
            }
            for (int x = 0; x < iw; x++, src++) {
                int wx = ix + x;
                if (wx < 0 || wx >= sw)
                    continue;
                int ci2 = idx[src];
                uint8_t *px = out + ((size_t)wy * sw + (size_t)wx) * 4;
                if (ci2 == transparent)
                    continue;         /* stays fully transparent (calloc zeroed it) */
                if (ct && ci2 * 3 + 2 < ctn * 3) {
                    px[0] = ct[ci2 * 3 + 0];
                    px[1] = ct[ci2 * 3 + 1];
                    px[2] = ct[ci2 * 3 + 2];
                }
                px[3] = 255;
            }
        }
        free(idx);
        break;                        /* the first image block is the preview */
    }

    *w = sw;
    *h = sh;
    return out;
}

/* ---------- format recognition and the one entry point ---------- */

int img_sniff(const uint8_t *mem, size_t n, char *kind)
{
    static const uint8_t png_magic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (kind)
        kind[0] = 0;
    if (!mem || n < 6)
        return 0;
    if (n >= 8 && memcmp(mem, png_magic, 8) == 0) {
        if (kind)
            memcpy(kind, "png", 4);
        return 1;
    }
    if (mem[0] == 0xff && mem[1] == 0xd8 && mem[2] == 0xff) {
        if (kind)
            memcpy(kind, "jpg", 4);
        return 1;
    }
    if (memcmp(mem, "GIF8", 4) == 0 && (mem[4] == '7' || mem[4] == '9') && mem[5] == 'a') {
        if (kind)
            memcpy(kind, "gif", 4);
        return 1;
    }
    if (n >= 12 && memcmp(mem, "RIFF", 4) == 0 && memcmp(mem + 8, "WEBP", 4) == 0) {
        if (kind)
            memcpy(kind, "webp", 5);
        return 1;
    }
    return 0;
}

uint8_t *img_decode_mem(const uint8_t *mem, size_t n, int *w, int *h, char *kind)
{
    char k[8] = "";
    *w = *h = 0;
    if (!img_sniff(mem, n, k)) {
        klog(L_DEBUG, "image: no decoder for these %zu bytes", n);
        return NULL;
    }
    if (kind)
        snprintf(kind, 8, "%s", k);
    uint8_t *out = NULL;
    if (!strcmp(k, "png"))
        out = img_decode_png(mem, n, w, h);
    else if (!strcmp(k, "jpg"))
        out = img_decode_jpeg(mem, n, w, h);
    else if (!strcmp(k, "gif"))
        out = img_decode_gif(mem, n, w, h);
    else if (!strcmp(k, "webp"))
        out = img_decode_webp(mem, n, w, h);
    if (!out)
        *w = *h = 0;
    return out;
}
