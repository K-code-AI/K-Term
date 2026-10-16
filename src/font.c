/* Kai-Term — FreeType glyph atlas (alpha8 texture data) + color emoji atlas (RGBA8).
 *
 * Mono glyphs: fontconfig fallback lookup -> FreeType raster -> alpha8 atlas.
 * Color emoji: the whole grapheme cluster is shaped by HarfBuzz into ONE glyph id of a color
 * emoji font (Noto Color Emoji: a CBDT/CBLC bitmap-only face), whose premultiplied BGRA bitmap
 * is converted to RGBA and packed into a second atlas. Metrics stay UNSCALED (the strike's own
 * pixel size); the renderer scales them per frame, so zoom never re-rasterizes an emoji. */
#include "kai-term.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#define ATLAS_W0 512
#define ATLAS_H0 512
/* Color emoji bitmaps are big (the installed Noto Color Emoji strike: 136x128 px per glyph), so the
   color atlas is wider than the mono one and its height is capped: 1024*8192*4 = 32 MB is already ~1900
   distinct emoji, and anything past that falls back to the mono glyph instead of growing the
   atlas forever (a `cat` of an emoji-heavy file must not exhaust memory). */
#define CATLAS_W0 1024
#define CATLAS_H0 512
#define CATLAS_H_MAX 8192
#define GLYPH_PAD 1

static unsigned cp_hash(uint32_t cp)
{
    cp *= 2654435761u;
    cp ^= cp >> 16;
    return cp;
}

static void tab_insert(Font *f, Glyph *g)
{
    unsigned i = cp_hash(g->cp) & f->tab_mask;
    while (f->tab[i])
        i = (i + 1) & f->tab_mask;
    f->tab[i] = g;
    f->tab_count++;
}

static void tab_rehash(Font *f)
{
    unsigned ncap = (f->tab_mask + 1) * 2;
    Glyph **ntab = calloc(ncap, sizeof(Glyph *));
    if (!ntab)
        abort();
    Glyph **old = f->tab;
    unsigned oldmask = f->tab_mask;
    f->tab = ntab;
    f->tab_mask = ncap - 1;
    f->tab_count = 0;
    for (unsigned i = 0; i <= oldmask; i++)
        if (old[i])
            tab_insert(f, old[i]);
    free(old);
}

static Glyph *tab_find(Font *f, uint32_t cp)
{
    unsigned i = cp_hash(cp) & f->tab_mask;
    while (f->tab[i]) {
        if (f->tab[i]->cp == cp)
            return f->tab[i];
        i = (i + 1) & f->tab_mask;
    }
    return NULL;
}

/* place glyph bits into the atlas (shelf packer) */
static void atlas_place_bits(Font *f, Glyph *g, const uint8_t *bits, int pitch)
{
    int w = g->w + GLYPH_PAD;
    if (f->shelf_x + w > f->atlas_w) {
        f->shelf_y += f->shelf_h;
        f->shelf_x = 0;
        f->shelf_h = 0;
    }
    g->u0 = (float)(f->shelf_x + GLYPH_PAD) / f->atlas_w;
    g->v0 = (float)(f->shelf_y + GLYPH_PAD) / f->atlas_h;
    g->u1 = (float)(f->shelf_x + GLYPH_PAD + g->w) / f->atlas_w;
    g->v1 = (float)(f->shelf_y + GLYPH_PAD + g->h) / f->atlas_h;
    for (int y = 0; y < g->h; y++)
        memcpy(f->atlas + (size_t)(f->shelf_y + GLYPH_PAD + y) * f->atlas_w +
                   f->shelf_x + GLYPH_PAD,
               bits + (size_t)y * pitch, (size_t)g->w);
    if (g->h + GLYPH_PAD > f->shelf_h)
        f->shelf_h = g->h + GLYPH_PAD;
    f->shelf_x += w;
    f->atlas_dirty = 1;
}

/* Would a w*h glyph still fit at the current shelf cursor? atlas_place_bits starts a NEW
   shelf when the row is full, so the check must account for that vertical step — comparing
   against the CURRENT shelf_y alone lets a wrapped glyph be written past the end of the
   buffer (heap overflow, caught by ASan in the color atlas where glyphs are 136px wide). */
static int atlas_fits(Font *f, int w, int h)
{
    int x = f->shelf_x, y = f->shelf_y, sh = f->shelf_h;
    if (x + w + GLYPH_PAD > f->atlas_w) {   /* the packer would wrap to a new shelf */
        x = 0;
        y += sh;
    }
    return y + h + GLYPH_PAD <= f->atlas_h;
}

static int glyph_raster(Font *f, Glyph *g, void *face, int allow_grow);

/* double the atlas height and re-render every cached glyph from the face it came from
   (primary or a fallback face) so fallback glyphs keep their shape. Repeats until every
   glyph fits, so the packer can never write outside the buffer. */
static void atlas_grow(Font *f)
{
    for (;;) {
        int nh = f->atlas_h * 2;
        uint8_t *na = calloc((size_t)f->atlas_w * (size_t)nh, 1);
        if (!na)
            abort();
        free(f->atlas);
        f->atlas = na;
        f->atlas_h = nh;
        f->shelf_x = f->shelf_y = f->shelf_h = 0;

        int all_fit = 1;
        for (unsigned i = 0; i <= f->tab_mask; i++) {
            Glyph *g = f->tab[i];
            if (!g || (g->w == 0 && g->h == 0))
                continue;
            if (!atlas_fits(f, g->w, g->h)) {
                all_fit = 0;
                g->w = g->h = 0;   /* no room: invalidate so the renderer skips this glyph */
                continue;
            }
            FT_Face face = g->face ? (FT_Face)g->face : (FT_Face)f->ft;
            if (FT_Load_Char(face, g->cp,
                             FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0)
                continue;
            FT_GlyphSlot sl = face->glyph;
            g->w = (int)sl->bitmap.width;
            g->h = (int)sl->bitmap.rows;
            g->bearing_x = sl->bitmap_left;
            g->bearing_y = sl->bitmap_top;
            g->advance = (int)((float)sl->advance.x / 64.0f * f->scale + 0.5f);
            if (g->w > 0 && g->h > 0)
                atlas_place_bits(f, g, sl->bitmap.buffer, (int)sl->bitmap.pitch);
        }
        if (all_fit)
            break;
    }
    f->atlas_dirty = 1;
}

static int glyph_raster(Font *f, Glyph *g, void *face, int allow_grow)
{
    if (!face ||
        FT_Load_Char((FT_Face)face, g->cp,
                     FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
        g->w = g->h = 0;
        g->u0 = g->v0 = 0.0f;
        g->u1 = g->v1 = 1.0f;   /* dummy white pixel */
        g->face = NULL;
        return 0;
    }
    FT_GlyphSlot sl = ((FT_Face)face)->glyph;
    g->face = face;
    g->w = (int)sl->bitmap.width;
    g->h = (int)sl->bitmap.rows;
    g->bearing_x = sl->bitmap_left;
    g->bearing_y = sl->bitmap_top;
    g->advance = (int)((float)sl->advance.x / 64.0f * f->scale + 0.5f);

    if (g->w == 0 || g->h == 0) {
        g->u0 = g->v0 = 0.0f;
        g->u1 = g->v1 = 1.0f;   /* dummy white pixel at (0,0) */
        return 0;
    }

    if (allow_grow &&
        ((g->w + GLYPH_PAD) * 2 > f->atlas_w || !atlas_fits(f, g->w, g->h))) {
        atlas_grow(f);
        return glyph_raster(f, g, face, 0);
    }
    if (!atlas_fits(f, g->w, g->h)) {   /* safety net: never write outside the atlas */
        g->w = g->h = 0;
        g->u0 = g->v0 = 0.0f;
        g->u1 = g->v1 = 1.0f;
        return 0;
    }

    /* convert bitmap to 8-bit alpha if mono */
    if (sl->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
        static uint8_t tmp[16384];
        if (g->w * g->h > (int)sizeof(tmp)) {
            g->w = g->h = 0;
            g->u0 = g->v0 = 0.0f;
            g->u1 = g->v1 = 1.0f;
            return 0;
        }
        for (int y = 0; y < g->h; y++) {
            const unsigned char *row = sl->bitmap.buffer + (size_t)y * sl->bitmap.pitch;
            for (int x = 0; x < g->w; x++)
                tmp[y * g->w + x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
        }
        atlas_place_bits(f, g, tmp, g->w);
    } else {
        atlas_place_bits(f, g, sl->bitmap.buffer, (int)sl->bitmap.pitch);
    }
    return 0;
}

/* Ask fontconfig for the best font covering cp. With prefer_mono, bias toward a monospace
   family so terminal columns stay aligned when a monospace font has the glyph; without it,
   accept any font that covers cp. Writes the resolved file path to out (empty if none). */
static void fc_lookup(uint32_t cp, int prefer_mono, char *out, size_t cap)
{
    out[0] = 0;
    FcCharSet *cs = FcCharSetCreate();
    if (!cs)
        return;
    FcCharSetAddChar(cs, cp);
    FcPattern *pat = FcPatternCreate();
    if (pat) {
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        if (prefer_mono)
            FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)"monospace");
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        FcPattern *match = FcFontMatch(NULL, pat, &res);
        if (match) {
            FcChar8 *file = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
                snprintf(out, cap, "%s", (const char *)file);
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pat);
    }
    FcCharSetDestroy(cs);
}

/* Load (or reuse from the cache) a fallback face for path, sized to the current px.
   Returns the FT_Face or NULL on failure. */
static FT_Face fb_load_face(Font *f, const char *path)
{
    for (int i = 0; i < f->nfb; i++)
        if (!strcmp(f->fb[i].path, path))
            return (FT_Face)f->fb[i].face;

    FT_Face face = NULL;
    if (FT_New_Face((FT_Library)f->ftlib, path, 0, &face)) {
        klog(L_WARN, "fallback FT_New_Face failed for %s", path);
        return NULL;
    }
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px)) {
        FT_Done_Face(face);
        return NULL;
    }
    if (f->nfb == f->fbcap) {
        int ncap = f->fbcap ? f->fbcap * 2 : 8;
        FallbackFace *nf = realloc(f->fb, (size_t)ncap * sizeof(FallbackFace));
        if (!nf) {
            FT_Done_Face(face);
            abort();
        }
        f->fb = nf;
        f->fbcap = ncap;
    }
    snprintf(f->fb[f->nfb].path, sizeof(f->fb[f->nfb].path), "%s", path);
    f->fb[f->nfb].face = face;
    f->nfb++;
    return face;
}

/* Pick the face to rasterize cp from. If the primary face covers it, use the primary.
   Otherwise ask fontconfig for a fallback font that actually contains cp: first a monospace
   match, then (if that match does not really cover cp) any font. A face is only used when
   FT_Get_Char_Index confirms it has the glyph, so a monospace font that merely sorts best but
   lacks the codepoint is not mistaken for a fallback. If nothing on the system covers cp, the
   primary face is returned and the glyph rasterizes as .notdef/blank (best effort). */
static void *font_find_face(Font *f, uint32_t cp)
{
    FT_Face primary = (FT_Face)f->ft;
    if (primary && FT_Get_Char_Index(primary, cp) != 0)
        return primary;
    if (!f->fc_ready)
        return primary;

    char path[4096] = "";
    fc_lookup(cp, 1, path, sizeof(path));
    if (path[0]) {
        FT_Face face = fb_load_face(f, path);
        if (face && FT_Get_Char_Index(face, cp) != 0) {
            klog(L_DEBUG, "fallback face for U+%04X: %s", cp, path);
            return face;
        }
    }

    char path2[4096] = "";
    fc_lookup(cp, 0, path2, sizeof(path2));
    if (path2[0] && strcmp(path2, path)) {
        FT_Face face = fb_load_face(f, path2);
        if (face && FT_Get_Char_Index(face, cp) != 0) {
            klog(L_DEBUG, "fallback face for U+%04X: %s", cp, path2);
            return face;
        }
    }

    return primary;   /* nothing covers cp: rasterize from primary (.notdef/blank) */
}

/* ---------------- color emoji ---------------- */

/* Cache of shaped color glyphs, keyed by the whole codepoint sequence of the cluster (not by
   codepoint): 👍 and 👍🏽 and 👨\u200d👩\u200d👧\u200d👦 are three different glyphs. */
static unsigned seq_hash(const uint32_t *s, int n)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) {
        h ^= s[i];
        h *= 16777619u;
        h ^= h >> 15;
    }
    return h;
}

static int seq_eq(const ColorGlyph *g, const uint32_t *seq, int n)
{
    if (g->nseq != n)
        return 0;
    for (int i = 0; i < n; i++)
        if (g->seq[i] != seq[i])
            return 0;
    return 1;
}

static void ctab_insert(Font *f, ColorGlyph *g)
{
    unsigned i = seq_hash(g->seq, g->nseq) & f->ctab_mask;
    while (f->ctab[i])
        i = (i + 1) & f->ctab_mask;
    f->ctab[i] = g;
    f->ctab_count++;
}

static void ctab_rehash(Font *f)
{
    unsigned ncap = (f->ctab_mask + 1) * 2;
    ColorGlyph **ntab = calloc(ncap, sizeof(ColorGlyph *));
    if (!ntab)
        abort();
    ColorGlyph **old = f->ctab;
    unsigned oldmask = f->ctab_mask;
    f->ctab = ntab;
    f->ctab_mask = ncap - 1;
    f->ctab_count = 0;
    for (unsigned i = 0; i <= oldmask; i++)
        if (old[i])
            ctab_insert(f, old[i]);
    free(old);
}

static ColorGlyph *ctab_find(Font *f, const uint32_t *seq, int n)
{
    unsigned i = seq_hash(seq, n) & f->ctab_mask;
    while (f->ctab[i]) {
        if (seq_eq(f->ctab[i], seq, n))
            return f->ctab[i];
        i = (i + 1) & f->ctab_mask;
    }
    return NULL;
}

/* pack RGBA8 bits into the color atlas (same shelf packer as the mono atlas) */
static void catlas_place_bits(Font *f, ColorGlyph *g, const uint8_t *rgba, int pitch)
{
    int w = g->w + GLYPH_PAD;
    if (f->cshelf_x + w > f->catlas_w) {
        f->cshelf_y += f->cshelf_h;
        f->cshelf_x = 0;
        f->cshelf_h = 0;
    }
    g->u0 = (float)(f->cshelf_x + GLYPH_PAD) / f->catlas_w;
    g->v0 = (float)(f->cshelf_y + GLYPH_PAD) / f->catlas_h;
    g->u1 = (float)(f->cshelf_x + GLYPH_PAD + g->w) / f->catlas_w;
    g->v1 = (float)(f->cshelf_y + GLYPH_PAD + g->h) / f->catlas_h;
    for (int y = 0; y < g->h; y++)
        memcpy(f->catlas + (size_t)(f->cshelf_y + GLYPH_PAD + y) * (size_t)f->catlas_w * 4 +
                   (size_t)(f->cshelf_x + GLYPH_PAD) * 4,
               rgba + (size_t)y * (size_t)pitch, (size_t)g->w * 4);
    if (g->h + GLYPH_PAD > f->cshelf_h)
        f->cshelf_h = g->h + GLYPH_PAD;
    f->cshelf_x += w;
    f->catlas_dirty = 1;
}

/* see atlas_fits: the same shelf-wrap check for the color atlas */
static int catlas_fits(Font *f, int w, int h)
{
    int x = f->cshelf_x, y = f->cshelf_y, sh = f->cshelf_h;
    if (x + w + GLYPH_PAD > f->catlas_w) {
        x = 0;
        y += sh;
    }
    return y + h + GLYPH_PAD <= f->catlas_h;
}

static int color_load_bits(Font *f, ColorGlyph *g, int allow_grow);

/* double the color atlas height and re-pack every cached color glyph from its glyph id.
   Repeats until every glyph fits: emoji bitmaps are ~136px wide, so a 512px atlas holds only
   three per shelf and the packer wraps often — without the fit check a wrapped glyph used to
   be written past the end of the buffer (ASan heap-buffer-overflow). */
static void catlas_grow(Font *f)
{
    for (;;) {
        int nh = f->catlas_h * 2;
        if (nh > CATLAS_H_MAX)
            nh = CATLAS_H_MAX;             /* clamp to the memory cap */
        if (nh <= f->catlas_h)
            break;                         /* already at the cap: leave the rest to the mono path */
        uint8_t *na = calloc((size_t)f->catlas_w * (size_t)nh * 4, 1);
        if (!na)
            abort();
        free(f->catlas);
        f->catlas = na;
        f->catlas_h = nh;
        f->cshelf_x = f->cshelf_y = f->cshelf_h = 0;

        int all_fit = 1;
        for (unsigned i = 0; i <= f->ctab_mask; i++) {
            ColorGlyph *g = f->ctab[i];
            if (!g || g->gid == 0)
                continue;
            if (!catlas_fits(f, g->w, g->h)) {
                all_fit = 0;
                g->w = g->h = 0;   /* no room: invalidate (stale texcoords must never be drawn) */
                continue;
            }
            color_load_bits(f, g, 0);   /* re-load the same glyph id into the bigger atlas */
        }
        if (all_fit)
            break;
    }
    f->catlas_dirty = 1;
}

/* Load the color bitmap for g->gid and pack it. FreeType hands CBDT/CBLC art over as a
   premultiplied BGRA bitmap; the atlas stores RGBA so the renderer can upload it straight to
   GL. Returns 1 when the glyph has usable bits. */
static int color_load_bits(Font *f, ColorGlyph *g, int allow_grow)
{
    FT_Face face = (FT_Face)f->cface;
    if (!face || FT_Load_Glyph(face, (FT_UInt)g->gid, FT_LOAD_COLOR | FT_LOAD_NO_SCALE) != 0) {
        g->w = g->h = 0;
        return 0;
    }
    FT_GlyphSlot sl = face->glyph;
    FT_Bitmap *bm = &sl->bitmap;
    if (bm->pixel_mode != FT_PIXEL_MODE_BGRA || !bm->buffer || bm->rows <= 0 ||
        bm->width <= 0 || bm->pitch <= 0) {
        g->w = g->h = 0;
        return 0;
    }
    g->w = (int)bm->width;
    g->h = (int)bm->rows;
    g->bearing_x = sl->bitmap_left;
    g->bearing_y = sl->bitmap_top;

    if (allow_grow &&
        ((g->w + GLYPH_PAD) * 2 > f->catlas_w || !catlas_fits(f, g->w, g->h))) {
        catlas_grow(f);
        return color_load_bits(f, g, 0);
    }
    if (!catlas_fits(f, g->w, g->h)) {   /* safety net: never write outside the atlas */
        g->w = g->h = 0;
        return 0;
    }

    size_t n = (size_t)g->w * (size_t)g->h * 4;
    uint8_t *rgba = malloc(n);
    if (!rgba)
        abort();
    for (int y = 0; y < g->h; y++) {
        const uint8_t *src = bm->buffer + (size_t)y * (size_t)bm->pitch;
        uint8_t *dst = rgba + (size_t)y * (size_t)g->w * 4;
        for (int x = 0; x < g->w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];   /* R (BGRA byte 2) */
            dst[x * 4 + 1] = src[x * 4 + 1];   /* G */
            dst[x * 4 + 2] = src[x * 4 + 0];   /* B (BGRA byte 0) */
            dst[x * 4 + 3] = src[x * 4 + 3];   /* A (premultiplied, kept as-is) */
        }
    }
    catlas_place_bits(f, g, rgba, g->w * 4);
    free(rgba);
    return 1;
}

/* Ask fontconfig for a COLOR font covering cp (FC_COLOR=FcTrue). On this box that is
   NotoColorEmoji.ttf; on a box without any color emoji font it finds nothing and color emoji
   stay disabled (the mono fallback still draws the emoji). */
static void fc_color_lookup(uint32_t cp, char *out, size_t cap)
{
    out[0] = 0;
    FcCharSet *cs = FcCharSetCreate();
    if (!cs)
        return;
    FcCharSetAddChar(cs, cp);
    FcPattern *pat = FcPatternCreate();
    if (pat) {
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        FcPatternAddBool(pat, FC_COLOR, FcTrue);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        FcPattern *match = FcFontMatch(NULL, pat, &res);
        if (match) {
            FcChar8 *file = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
                snprintf(out, cap, "%s", (const char *)file);
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pat);
    }
    FcCharSetDestroy(cs);
}

/* Open the color emoji face and build the HarfBuzz font that shapes clusters against it.
   The installed Noto Color Emoji is bitmap-only (FT_IS_SCALABLE == 0) with ONE CBDT strike whose
   bitmaps are 136x128 px, so the strike is selected with FT_Select_Size and bitmaps are loaded
   unscaled; a scalable color font
   (COLR/SVG) is sized to the cell instead. color_ok stays 0 when the system has no color font,
   when FreeType was built without color-bitmap support, or when HarfBuzz cannot wrap the face —
   in every one of those cases the mono fallback still draws the emoji. */
static void color_init(Font *f)
{
    if (!f->fc_ready)
        return;

    char path[4096] = "";
    fc_color_lookup(0x1F600, path, sizeof(path));
    if (!path[0]) {
        klog(L_INFO, "no color emoji font found; emoji stay monochrome");
        return;
    }

    FT_Face face = NULL;
    if (FT_New_Face((FT_Library)f->ftlib, path, 0, &face)) {
        klog(L_WARN, "color emoji FT_New_Face failed for %s", path);
        return;
    }
    if (!FT_HAS_COLOR(face)) {
        klog(L_INFO, "%s has no color bitmaps; emoji stay monochrome", path);
        FT_Done_Face(face);
        return;
    }
    if (FT_IS_SCALABLE(face)) {
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px)) {
            FT_Done_Face(face);
            return;
        }
        f->color_px = f->px;
    } else {
        if (FT_Select_Size(face, 0)) {
            klog(L_WARN, "FT_Select_Size failed for %s", path);
            FT_Done_Face(face);
            return;
        }
        /* size metrics are 26.6 fixed point; this is the strike's own line height, i.e. the
           pixel size the unscaled bitmaps are drawn at. It comes from the font, not from here:
           128 for the installed Noto Color Emoji, and the L_INFO line below logs what a given
           box really reports. */
        f->color_px = (int)(face->size->metrics.height / 64);
        if (f->color_px <= 0)
            f->color_px = (int)face->size->metrics.y_ppem;
        /* Last resort for a color face that reports no usable metrics at all. color_px is the
           divisor in render_draw's `cell_h / color_px`, so it must stay > 0; assuming the text
           pixel size makes the emoji about one cell tall instead of trusting a hard-coded strike
           height (font_size is clamped to 6..256 in config.c, so f->px is always > 0). Never taken
           with the installed font: height/64 = 128 and y_ppem = 109 there. */
        if (f->color_px <= 0)
            f->color_px = f->px;
    }

    hb_font_t *hf = hb_ft_font_create_referenced(face);
    if (!hf) {
        klog(L_WARN, "hb_ft_font_create_referenced failed for %s", path);
        FT_Done_Face(face);
        return;
    }

    f->cface = face;
    f->hbf = hf;
    f->catlas_w = CATLAS_W0;
    f->catlas_h = CATLAS_H0;
    f->catlas = calloc((size_t)f->catlas_w * (size_t)f->catlas_h * 4, 1);
    if (!f->catlas)
        abort();
    f->cshelf_x = 0;
    f->cshelf_y = 0;
    f->cshelf_h = 0;
    f->ctab_mask = 255;
    f->ctab = calloc(f->ctab_mask + 1, sizeof(ColorGlyph *));
    if (!f->ctab)
        abort();
    f->color_ok = 1;
    klog(L_INFO, "color emoji: %s (%dpx strike)", path, f->color_px);
}

/* Shape one emoji grapheme cluster into a single glyph id (HarfBuzz applies the font's GSUB
   rules, so 👨\u200d👩\u200d👧\u200d👦, 🏳️\u200d🌈 and 👍🏽 each become ONE glyph), load that glyph's
   color bitmap into the color atlas and cache it under the cluster's codepoint sequence.
   Returns NULL (caller falls back to the mono glyph) when color emoji are unavailable, when
   shaping does not collapse to exactly one real glyph, or when the glyph has no color bits. */
ColorGlyph *font_color_glyph(Font *f, const uint32_t *seq, int n)
{
    if (!f->color_ok || !f->hbf || n <= 0 || n > CLUSTER_MAX + 1)
        return NULL;

    ColorGlyph *g = ctab_find(f, seq, n);
    if (g)
        return g->w > 0 && g->h > 0 ? g : NULL;

    hb_buffer_t *b = hb_buffer_create();
    if (!b)
        return NULL;
    hb_buffer_add_codepoints(b, seq, n, 0, -1);
    hb_buffer_set_direction(b, HB_DIRECTION_LTR);
    hb_buffer_set_script(b, HB_SCRIPT_COMMON);
    hb_buffer_set_language(b, hb_language_from_string("C", -1));
    hb_shape((hb_font_t *)f->hbf, b, NULL, 0);

    unsigned ng = 0;
    hb_glyph_info_t *gi = hb_buffer_get_glyph_infos(b, &ng);
    unsigned gid = 0;
    if (ng == 1 && gi && gi[0].codepoint != 0)
        gid = gi[0].codepoint;
    hb_buffer_destroy(b);

    /* Negative results are cached too (w=h=0, gid=0), so a codepoint that the color font does
       not own is shaped exactly once instead of on every frame. */
    g = calloc(1, sizeof(ColorGlyph));
    if (!g)
        abort();
    g->nseq = n;
    for (int i = 0; i < n; i++)
        g->seq[i] = seq[i];
    g->gid = gid;
    if (gid)
        color_load_bits(f, g, 1);
    ctab_insert(f, g);
    if (f->ctab_count * 4 > (f->ctab_mask + 1) * 3)
        ctab_rehash(f);
    return g->w > 0 && g->h > 0 ? g : NULL;
}

Glyph *font_glyph(Font *f, uint32_t cp)
{
    if (cp == ' ') {
        static Glyph space;
        /* one shared blank glyph (advance handled by cell width) */
        if (space.cp != cp) {
            space.cp = cp;
            space.w = space.h = 0;
            space.bearing_x = space.bearing_y = 0;
            space.advance = f->cell_w;
            space.u0 = space.v0 = 0.0f;
            space.u1 = space.v1 = 1.0f;
            space.face = NULL;
        }
        return &space;
    }

    Glyph *g = tab_find(f, cp);
    if (g)
        return g;

    g = calloc(1, sizeof(Glyph));
    if (!g)
        abort();
    g->cp = cp;
    glyph_raster(f, g, font_find_face(f, cp), 1);
    tab_insert(f, g);
    if (f->tab_count * 4 > (f->tab_mask + 1) * 3)
        tab_rehash(f);
    return g;
}

/* Compute cell metrics from the current face size (face must already be sized to px).
   face->ascender/descender are in font units (upm), NOT pixels.
   face->size->metrics.ascender/descender/height are in 26.6 fixed-point (divide by 64). */
static void font_metrics(Font *f, int px)
{
    FT_Face face = (FT_Face)f->ft;
    if (FT_Load_Char(face, '0', FT_LOAD_RENDER) == 0)
        f->cell_w = (int)(face->glyph->advance.x / 64);
    if (f->cell_w <= 0)
        f->cell_w = px / 2;
    if (face->size->metrics.ascender > 0)
        f->ascent = (int)(face->size->metrics.ascender / 64);
    else
        f->ascent = (int)((float)face->ascender / face->units_per_EM * px + 0.5f);
    if (face->size->metrics.descender < 0)
        f->descent = (int)(-(face->size->metrics.descender) / 64);
    else
        f->descent = (int)(-(float)face->descender / face->units_per_EM * px + 0.5f);
    if (face->size->metrics.height > 0)
        f->cell_h = (int)(face->size->metrics.height / 64);
    else
        f->cell_h = (int)(f->ascent + f->descent);
    if (f->cell_h < px)
        f->cell_h = px;
}

int font_init(Font *f, const char *pattern, int px)
{
    memset(f, 0, sizeof(*f));
    f->scale = 1.0f;
    f->px = px;

    if (FT_Init_FreeType((FT_Library *)&f->ftlib)) {
        klog(L_ERROR, "FT_Init_FreeType failed");
        return -1;
    }

    char path[4096] = "";
    if (FcInit()) {
        f->fc_ready = 1;   /* keep fontconfig loaded for fallback lookups; FcFini() runs in font_destroy */
        FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
        if (pat) {
            FcConfigSubstitute(NULL, pat, FcMatchPattern);
            FcDefaultSubstitute(pat);
            FcResult res;
            FcPattern *match = FcFontMatch(NULL, pat, &res);
            if (match) {
                FcChar8 *file = NULL;
                if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
                    snprintf(path, sizeof(path), "%s", (const char *)file);
                FcPatternDestroy(match);
            }
            FcPatternDestroy(pat);
        }
    }
    if (!path[0]) {
        klog(L_ERROR, "no font match for '%s'", pattern);
        return -1;
    }

    if (FT_New_Face((FT_Library)f->ftlib, path, 0, (FT_Face *)&f->ft)) {
        klog(L_ERROR, "FT_New_Face failed for %s", path);
        return -1;
    }
    FT_Face face = (FT_Face)f->ft;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px)) {
        klog(L_ERROR, "FT_Set_Pixel_Sizes failed");
        return -1;
    }
    klog(L_INFO, "font: %s", path);

    font_metrics(f, px);

    /* color emoji support (HarfBuzz + a color emoji font); failure here is not fatal, the
       fontconfig mono fallback still draws emoji. */
    color_init(f);

    f->atlas_w = ATLAS_W0;
    f->atlas_h = ATLAS_H0;
    f->atlas = calloc((size_t)f->atlas_w * f->atlas_h, 1);
    if (!f->atlas)
        abort();
    f->atlas[0] = 255;   /* dummy white pixel at (0,0) */
    f->shelf_x = 1;      /* skip dummy pixel column */
    f->shelf_y = 0;
    f->shelf_h = 1;

    f->tab_mask = 1023;
    f->tab = calloc(f->tab_mask + 1, sizeof(Glyph *));
    if (!f->tab)
        abort();

    /* pre-rasterize printable ASCII so common text is instant */
    for (uint32_t c = 33; c < 127; c++)
        font_glyph(f, c);

    return 0;
}

int font_resize(Font *f, int px)
{
    if (!f->ft)
        return -1;
    FT_Face face = (FT_Face)f->ft;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px)) {
        klog(L_ERROR, "FT_Set_Pixel_Sizes(%d) failed", px);
        return -1;
    }
    font_metrics(f, px);
    f->px = px;

    /* Color emoji: a bitmap-only strike (the installed Noto Color Emoji: one strike, 136x128 px
       bitmaps) is loaded UNSCALED and scaled at draw time, so zooming needs nothing here — the
       atlas stays valid and the emoji stay as sharp as that single strike allows. A scalable color
       face (COLR/SVG) is sized like the text face, so its cached bitmaps are dropped and re-shaped
       at the new px. */
    if (f->color_ok && f->cface && FT_IS_SCALABLE((FT_Face)f->cface)) {
        if (FT_Set_Pixel_Sizes((FT_Face)f->cface, 0, (FT_UInt)px)) {
            klog(L_WARN, "FT_Set_Pixel_Sizes(%d) failed for the color face", px);
        } else {
            hb_ft_font_changed((hb_font_t *)f->hbf);
            f->color_px = px;
            for (unsigned i = 0; i <= f->ctab_mask; i++)
                free(f->ctab[i]);
            memset(f->ctab, 0, (size_t)(f->ctab_mask + 1) * sizeof(ColorGlyph *));
            f->ctab_count = 0;
            free(f->catlas);
            f->catlas = calloc((size_t)f->catlas_w * (size_t)f->catlas_h * 4, 1);
            if (!f->catlas)
                abort();
            f->cshelf_x = f->cshelf_y = f->cshelf_h = 0;
            f->catlas_dirty = 1;
        }
    }

    /* Fallback faces were sized for the old pixel size; drop them so the next lookup
       reloads them at the new size (glyphs below are freed and re-rasterized anyway). */
    for (int i = 0; i < f->nfb; i++)
        if (f->fb[i].face)
            FT_Done_Face((FT_Face)f->fb[i].face);
    f->nfb = 0;

    /* Drop every cached glyph and rebuild the atlas at the new size. The renderer
       re-uploads the texture because atlas_dirty is set (atlas_w/h reset to base). */
    for (unsigned i = 0; i <= f->tab_mask; i++)
        free(f->tab[i]);
    memset(f->tab, 0, (size_t)(f->tab_mask + 1) * sizeof(Glyph *));
    f->tab_count = 0;

    free(f->atlas);
    f->atlas_w = ATLAS_W0;
    f->atlas_h = ATLAS_H0;
    f->atlas = calloc((size_t)f->atlas_w * f->atlas_h, 1);
    if (!f->atlas)
        abort();
    f->atlas[0] = 255;   /* dummy white pixel at (0,0) */
    f->shelf_x = 1;
    f->shelf_y = 0;
    f->shelf_h = 1;
    f->atlas_dirty = 1;

    for (uint32_t c = 33; c < 127; c++)
        font_glyph(f, c);
    return 0;
}

void font_destroy(Font *f)
{
    for (int i = 0; i < f->nfb; i++)
        if (f->fb[i].face)
            FT_Done_Face((FT_Face)f->fb[i].face);
    free(f->fb);
    /* the hb font holds a reference to the color face: destroy it before the face */
    if (f->hbf)
        hb_font_destroy((hb_font_t *)f->hbf);
    if (f->cface)
        FT_Done_Face((FT_Face)f->cface);
    if (f->ft)
        FT_Done_Face((FT_Face)f->ft);
    if (f->ftlib)
        FT_Done_FreeType((FT_Library)f->ftlib);
    for (unsigned i = 0; i <= f->tab_mask; i++)
        free(f->tab[i]);
    free(f->tab);
    free(f->atlas);
    for (unsigned i = 0; i <= f->ctab_mask; i++)
        free(f->ctab[i]);
    free(f->ctab);
    free(f->catlas);
    if (f->fc_ready)
        FcFini();
    memset(f, 0, sizeof(*f));
}
