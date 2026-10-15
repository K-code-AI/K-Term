/* K-Term — renderer: EGL + OpenGL 2.x, one textured-quad batch per frame.
 *
 * Each visible cell emits:
 *   - a background quad (samples dummy white atlas pixel, color = cell bg)
 *   - a glyph quad: a mono glyph (alpha8 atlas, tinted by the cell fg) or a COLOR emoji
 *     (RGBA8 color atlas, premultiplied, not tinted) — see a_glyph in the fragment shader
 *
 * Hardware instancing (GL_ARB_draw_instanced + GL_ARB_instanced_arrays) is used when the
 * driver advertises both extensions: a static unit-quad VBO supplies the per-vertex corner
 * (a_pos, divisor 0) and a per-instance buffer carries origin/size/tex/fg/bg/glyph (divisor 1),
 * drawn with glDrawArraysInstancedARB. When the extensions are unavailable the same instance
 * records are expanded into interleaved per-vertex data and drawn with glDrawArrays. Either
 * way it is a single draw call and the vertex shader is identical.
 */
/* GL/glext.h only declares the GL 2.x entry points when GL_GLEXT_PROTOTYPES is
   set; must be defined before any GL header is included. */
#define GL_GLEXT_PROTOTYPES 1

#include "k-term.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

/* Per-instance record layout (19 floats): origin(2) size(2) tex0(2) tex1(2) fg(4) bg(4) glyph(1)
   band0(1) band1(1). Per-vertex expanded layout (21 floats): corner(2) + the 19-float record.
   band0/band1 are the selection band's absolute pixel x edges (0 when no band); the fragment
   shader uses them to pick the background behind a glyph per-fragment (true sub-glyph selection). */
#define FLOATS_PER_INST 19
#define FLOATS_PER_VERT 21

static const char *vs_src =
    "#version 120\n"
    "attribute vec2 a_pos;\n"      /* unit quad corner (0 or 1) */
    "attribute vec2 a_origin;\n"   /* per-instance quad bottom-left */
    "attribute vec2 a_size;\n"     /* per-instance quad size */
    "attribute vec2 a_tex0;\n"     /* texcoord at corner (0,0) */
    "attribute vec2 a_tex1;\n"     /* texcoord at corner (1,1) */
    "attribute vec4 a_fg;\n"
    "attribute vec4 a_bg;\n"
    "attribute float a_glyph;\n"
    "attribute float a_band0;\n"   /* selection band left edge (absolute px), 0 if none */
    "attribute float a_band1;\n"   /* selection band right edge (absolute px), 0 if none */
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_fg;\n"
    "varying vec4 v_bg;\n"
    "varying float v_glyph;\n"
    "varying float v_px;\n"        /* fragment absolute pixel x */
    "varying float v_band0;\n"
    "varying float v_band1;\n"
    "void main(void) {\n"
    "    vec2 p = a_origin + a_pos * a_size;\n"
    "    gl_Position = u_mvp * vec4(p, 0.0, 1.0);\n"
    "    v_tex = mix(a_tex0, a_tex1, a_pos);\n"
    "    v_fg = a_fg;\n"
    "    v_bg = a_bg;\n"
    "    v_glyph = a_glyph;\n"
    "    v_px = p.x;\n"
    "    v_band0 = a_band0;\n"
    "    v_band1 = a_band1;\n"
    "}\n";

static const char *fs_src =
    "#version 120\n"
    "uniform sampler2D u_atlas;\n"      /* unit 0: alpha8 mono glyph atlas */
    "uniform sampler2D u_catlas;\n"     /* unit 1: RGBA8 color emoji atlas */
    "uniform vec4 u_sel;\n"        /* selection highlight colour */
    "varying vec2 v_tex;\n"
    "varying vec4 v_fg;\n"
    "varying vec4 v_bg;\n"
    "varying float v_glyph;\n"
    "varying float v_px;\n"
    "varying float v_band0;\n"
    "varying float v_band1;\n"
    "void main(void) {\n"
    /* No GL blending: the shader composites manually as premul + bg*(1-a). For a glyph quad the
       background behind the glyph is chosen PER FRAGMENT: the selection colour where the fragment's
       pixel x falls inside the selection band, else the cell's own background. This lets a partially
       covered glyph show the highlight only over the covered sub-range (true sub-glyph selection)
       without enabling GL blending (which would regress the transparency feature).
       ALPHA: v_bg.a carries the window opacity (main.c multiplies the cell/selection/cursor colour's
       alpha by Config.opacity), so background pixels are see-through on a compositor. Ink must NOT be
       translucent: its own coverage decides the alpha there and only the background part of the
       fragment stays see-through. With opacity 1.0 every branch is byte-identical to the old
       behaviour (bg.a == 1 -> bgp == bg.rgb and a + 1*(1-a) == 1), so the opaque default cannot regress.
       PREMULTIPLICATION: a Wayland EGL ARGB buffer is premultiplied -- the compositor computes
       result = surface_rgb + (1 - surface_a) * backdrop -- so the colour channels must already carry
       the alpha. Writing the raw colour with a partial alpha (the old behaviour) adds the background at
       FULL strength over the backdrop, which is why a translucent window looked washed out. */
    "    vec4 bg = v_bg;\n"
    "    if (v_glyph > 0.5 && v_px >= v_band0 && v_px < v_band1)\n"
    "        bg = u_sel;\n"
    "    vec3 bgp = bg.rgb * bg.a;\n"      /* the background behind this fragment, premultiplied */
    /* a_glyph: 0 = background quad, 1 = mono glyph (alpha8, tinted by v_fg), 2 = COLOR emoji
       glyph. A color texel is already premultiplied RGBA straight out of the emoji font, so it
       composites as texel + bg*(1-texel.a) and is deliberately NOT tinted by the text colour;
       v_fg is white (0.5 grey for ATTR_DIM) so dimming still works without a new attribute. */
    "    if (v_glyph > 1.5) {\n"
    "        vec4 c = texture2D(u_catlas, v_tex) * v_fg;\n"
    "        gl_FragColor = vec4(c.rgb + bgp * (1.0 - c.a), c.a + bg.a * (1.0 - c.a));\n"
    "        return;\n"
    "    }\n"
    "    float a = texture2D(u_atlas, v_tex).r;\n"
    "    a *= v_glyph;\n"
    /* Source-over in premultiplied space. The alpha is the ink's coverage PLUS whatever of the
       (translucent) background is still visible under it -- NOT max(a, bg.a): that forced alpha 1 for
       every fragment inside a glyph quad's rectangle, so the bitmap bounding box (the counter of an
       'o', the margins around a thin glyph) was painted opaque near-black on a transparent window. */
    "    gl_FragColor = vec4(v_fg.rgb * a + bgp * (1.0 - a), a + bg.a * (1.0 - a));\n"
    "}\n";

/* unit quad geometry: two triangles in [0,1]^2 (per-vertex corners for the instanced path) */
static const float quad_verts[12] = {
    0, 0,  1, 0,  1, 1,
    0, 0,  1, 1,  0, 1,
};

typedef GLvoid (*fn_set_divisor)(GLuint, GLuint);
typedef GLvoid (*fn_draw_arrays_instanced)(GLenum, GLint, GLsizei, GLsizei);

static void col2f(uint32_t argb, float *rgba)
{
    rgba[0] = ((argb >> 16) & 0xff) / 255.0f;
    rgba[1] = ((argb >> 8) & 0xff) / 255.0f;
    rgba[2] = (argb & 0xff) / 255.0f;
    rgba[3] = ((argb >> 24) & 0xff) / 255.0f;
}

static unsigned compile_prog(Render *r)
{
    unsigned vs = glCreateShader(GL_VERTEX_SHADER);
    unsigned fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char *v = vs_src, *f = fs_src;
    glShaderSource(vs, 1, &v, NULL);
    glShaderSource(fs, 1, &f, NULL);
    glCompileShader(vs);
    glCompileShader(fs);
    int ok = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(vs, sizeof(log), NULL, log);
        klog(L_ERROR, "vertex shader: %s", log);
        return 0;
    }
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(fs, sizeof(log), NULL, log);
        klog(L_ERROR, "fragment shader: %s", log);
        return 0;
    }
    unsigned p = glCreateProgram();
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_origin");
    glBindAttribLocation(p, 2, "a_size");
    glBindAttribLocation(p, 3, "a_tex0");
    glBindAttribLocation(p, 4, "a_tex1");
    glBindAttribLocation(p, 5, "a_fg");
    glBindAttribLocation(p, 6, "a_bg");
    glBindAttribLocation(p, 7, "a_glyph");
    glBindAttribLocation(p, 8, "a_band0");
    glBindAttribLocation(p, 9, "a_band1");
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        klog(L_ERROR, "program link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    r->u_mvp = glGetUniformLocation(p, "u_mvp");
    r->u_atlas = glGetUniformLocation(p, "u_atlas");
    r->u_catlas = glGetUniformLocation(p, "u_catlas");
    r->u_sel = glGetUniformLocation(p, "u_sel");
    return p;
}

static void setup_gl(Render *r)
{
    glGenVertexArrays(1, &r->vao);
    glBindVertexArray(r->vao);

    /* static unit-quad corner buffer (a_pos) for the instanced path */
    glGenBuffers(1, &r->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    /* dynamic buffer: per-instance records (instanced) or expanded per-vertex (fallback) */
    glGenBuffers(1, &r->vbo);

    for (unsigned i = 0; i < 10; i++)
        glEnableVertexAttribArray(i);

    /* Load the ARB instancing entry points. They are ARB extensions on a GL 2.1 context
       (merged into core only in GL 3.2), so fetch them via eglGetProcAddress and require
       BOTH extension names in GL_EXTENSIONS. There is no GL_ARB_vertex_instanced. */
    r->set_div = (void *)eglGetProcAddress("glVertexAttribDivisorARB");
    r->draw_inst = (void *)eglGetProcAddress("glDrawArraysInstancedARB");
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    r->inst_ok = (r->set_div && r->draw_inst && exts &&
                  strstr(exts, "GL_ARB_instanced_arrays") &&
                  strstr(exts, "GL_ARB_draw_instanced")) ? 1 : 0;
    if (r->inst_ok)
        klog(L_INFO, "instancing enabled (GL_ARB_draw_instanced + GL_ARB_instanced_arrays)");
    else
        klog(L_INFO, "instancing unavailable; using per-vertex expansion");
}

int render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity)
{
    memset(r, 0, sizeof(*r));
    r->vw = w;
    r->vh = h;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)(uintptr_t)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy;
    if (get_platform)
        dpy = get_platform(EGL_PLATFORM_WAYLAND_EXT, wl_display, NULL);
    else
        dpy = eglGetDisplay((EGLNativeDisplayType)wl_display);
    if (dpy == EGL_NO_DISPLAY) {
        klog(L_ERROR, "eglGetDisplay failed");
        return -1;
    }
    EGLint vmaj, vmin;
    if (!eglInitialize(dpy, &vmaj, &vmin)) {
        klog(L_ERROR, "eglInitialize failed: 0x%x", eglGetError());
        return -1;
    }

    /* Wayland transparency needs an ARGB window visual + alpha in the clear
       colour; no EGL extension attributes are required (EGL_TRANSPARENT_*_EXT
       is an X11-era extension and is absent from the installed eglext.h). */
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    const int cfg_n = (int)(sizeof(cfg_attrs) / sizeof(cfg_attrs[0]));

    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg == 0) {
        /* Fallback: drop the EGL_ALPHA_SIZE attribute pair (compositor/driver
           without an ARGB visual). Build it by element count, not sizeof bytes. */
        EGLint fallback[16];
        int fn = 0;
        for (int i = 0; i < cfg_n; i++) {
            if (cfg_attrs[i] == EGL_ALPHA_SIZE) { i++; continue; }  /* skip attr + its value */
            fallback[fn++] = cfg_attrs[i];
        }
        fallback[fn++] = EGL_NONE;
        if (!eglChooseConfig(dpy, fallback, &cfg, 1, &ncfg) || ncfg == 0) {
            klog(L_ERROR, "eglChooseConfig failed: 0x%x", eglGetError());
            return -1;
        }
    }

    EGLint win_id = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &win_id);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)egl_window, NULL);
    if (surf == EGL_NO_SURFACE) {
        klog(L_ERROR, "eglCreateWindowSurface failed: 0x%x", eglGetError());
        return -1;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        klog(L_ERROR, "eglBindAPI(EGL_OPENGL_API) failed");
        return -1;
    }
    EGLint ctx_attrs[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        ctx_attrs[3] = 0;
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    }
    if (ctx == EGL_NO_CONTEXT) {
        klog(L_ERROR, "eglCreateContext failed: 0x%x", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        klog(L_ERROR, "eglMakeCurrent failed: 0x%x", eglGetError());
        return -1;
    }

    r->edpy = dpy;
    r->esurf = surf;
    r->ectx = ctx;
    r->nwin = egl_window;
    r->opacity = fclamp_(opacity, 0.0f, 1.0f);
    r->ecfg = malloc(sizeof(cfg));
    if (r->ecfg)
        memcpy(r->ecfg, &cfg, sizeof(cfg));
    (void)win_id;

    klog(L_INFO, "GL: %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    r->prog = compile_prog(r);
    if (!r->prog)
        return -1;
    setup_gl(r);

    float bg_r, bg_g, bg_b;
    bg_r = ((DEF_BG >> 16) & 0xff) / 255.0f;
    bg_g = ((DEF_BG >> 8) & 0xff) / 255.0f;
    bg_b = (DEF_BG & 0xff) / 255.0f;
    /* The buffer is premultiplied, so the clear colour must be too: the leftover pixels at the
       bottom/right of the window (a window size that is not an exact multiple of the cell) then blend
       exactly like the cell backgrounds render_draw paints. */
    glClearColor(bg_r * opacity, bg_g * opacity, bg_b * opacity, opacity);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Do NOT swap here: attaching a buffer before the first xdg_surface.configure is
       acked is a Wayland protocol error. The first eglSwapBuffers happens in render_draw. */
    (void)win_id;
    return 0;
}

static float *ensure_vdata(Render *r, int rows, int cols)
{
    /* worst case per cell: bg quad + selection overlay quad + glyph quad = 3 records */
    int need = rows * cols * 3 * FLOATS_PER_INST;
    if (need > r->vcap) {
        free(r->vdata);
        r->vcap = need;
        r->vdata = malloc((size_t)need * sizeof(float));
        if (!r->vdata)
            abort();
    }
    return r->vdata;
}

static float *ensure_edata(Render *r, int rows, int cols)
{
    /* fallback expansion: 3 quads/cell (bg + selection overlay + glyph) * 6 verts/quad */
    int need = rows * cols * 3 * 6 * FLOATS_PER_VERT;
    if (need > r->ecap) {
        free(r->edata);
        r->ecap = need;
        r->edata = malloc((size_t)need * sizeof(float));
        if (!r->edata)
            abort();
    }
    return r->edata;
}

/* emit one quad as a single per-instance record (19 floats).
   Texcoords are Y-flipped (atlas rows go top-down, GL y goes bottom-up): tex0 = corner(0,0)
   = (u0,v1), tex1 = corner(1,1) = (u1,v0). The shader derives the other two corners. */
static void emit_quad(float **ip, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      const float *fg, const float *bg, float glyph,
                      float band0, float band1)
{
    float *v = *ip;
    *v++ = x;  *v++ = y;                 /* origin */
    *v++ = w;  *v++ = h;                 /* size */
    *v++ = u0; *v++ = v1;                /* tex0 */
    *v++ = u1; *v++ = v0;                /* tex1 */
    *v++ = fg[0]; *v++ = fg[1]; *v++ = fg[2]; *v++ = fg[3];
    *v++ = bg[0]; *v++ = bg[1]; *v++ = bg[2]; *v++ = bg[3];
    *v++ = glyph;
    *v++ = band0;
    *v++ = band1;
    *ip = v;
}

/* expand per-instance records into interleaved per-vertex data (fallback path):
   for each instance, for each of the 6 unit corners, write corner(2) + record(19). */
static void expand_instances(const float *inst, int nquads, float *out)
{
    float *o = out;
    for (int i = 0; i < nquads; i++) {
        const float *rec = inst + i * FLOATS_PER_INST;
        for (int c = 0; c < 6; c++) {
            *o++ = quad_verts[c * 2];
            *o++ = quad_verts[c * 2 + 1];
            memcpy(o, rec, FLOATS_PER_INST * sizeof(float));
            o += FLOATS_PER_INST;
        }
    }
}

static void set_attribs_interleaved(void)
{
    const GLint s = FLOATS_PER_VERT * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s, (const void *)(0 * sizeof(float)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (const void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (const void *)(4 * sizeof(float)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, s, (const void *)(6 * sizeof(float)));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, s, (const void *)(8 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s, (const void *)(10 * sizeof(float)));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, s, (const void *)(14 * sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, s, (const void *)(18 * sizeof(float)));
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, s, (const void *)(19 * sizeof(float)));
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, s, (const void *)(20 * sizeof(float)));
}

static void set_attribs_instanced(Render *r)
{
    fn_set_divisor set_div = (fn_set_divisor)r->set_div;
    const GLint s = FLOATS_PER_INST * sizeof(float);
    /* a_pos from the static unit-quad VBO (per-vertex, divisor 0) */
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void *)0);
    set_div(0, 0);
    /* per-instance attributes from the dynamic VBO (divisor 1) */
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (const void *)(0 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (const void *)(2 * sizeof(float)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, s, (const void *)(4 * sizeof(float)));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, s, (const void *)(6 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s, (const void *)(8 * sizeof(float)));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, s, (const void *)(12 * sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, s, (const void *)(16 * sizeof(float)));
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, s, (const void *)(17 * sizeof(float)));
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, s, (const void *)(18 * sizeof(float)));
    for (unsigned i = 1; i < 10; i++)
        set_div(i, 1);
}

int render_draw(Render *r, Term *t, Font *f)
{
    Grid *g = &t->g;
    glClear(GL_COLOR_BUFFER_BIT);

    float *vdata = ensure_vdata(r, g->rows, g->cols);
    float fg[4], bg[4];
    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    float ascent = (float)f->ascent * f->scale;
    const float white[4] = { 1, 1, 1, 1 };
    float selc[4];
    col2f(SEL_BG, selc);
    /* Window transparency (Config.opacity): the cell/selection/cursor colours are opaque ARGB, so the
       alpha is scaled here and carried to the shader in a_bg.a. Ink keeps alpha 1 (see fs_src), only the
       background becomes see-through. opacity == 1 leaves every value untouched. */
    selc[3] *= r->opacity;

    SelSpan selspan;
    int sel_on = t->sel_active;
    if (sel_on)
        term_selection_span(t, &selspan);

    /* The grid is anchored at the TOP-LEFT of the window: viewport row y occupies window pixels
       [y*ch, (y+1)*ch) and column x occupies [x*cw, (x+1)*cw). main.c maps the pointer with
       row = ptr_y / cell_h, col = ptr_x / cell_w, which is only exact when the grid starts at window
       (0,0). The leftover pixels (cur_h - rows*cell_h, non-zero after almost every compositor
       configure: e.g. 10 px on a 16 px cell) therefore belong at the BOTTOM/right of the window and
       never at the top/left. Anchoring at the bottom-left instead put them at the top, so
       floor(ptr_y/cell_h) named the row BELOW the pointer for most of each row's height -- the
       selection (and SGR mouse reporting) landed on the wrong line. */
    float pad = (float)r->vh - (float)g->rows * ch;
    if (pad < 0.0f)
        pad = 0.0f;

    float *vp = vdata;
    int nquads = 0;
    const float du = 1.0f / (float)f->atlas_w, dv = 1.0f / (float)f->atlas_h;

    /* Three passes, back to front: (1) every cell's background, (2) the selection bands,
       (3) the glyphs. Drawing all backgrounds before any glyph is required now that font
       fallback makes WIDE glyphs (CJK/emoji) actually render: a wide glyph quad spans into the
       following cell, and the old per-cell order emitted that next cell's background quad AFTER
       the glyph, painting over (clipping) the glyph's right half. With backgrounds first, a
       glyph is never covered by a neighbouring background. Layer order is unchanged
       (background < selection band < glyph), so selection legibility and the cursor block are
       identical to before. */

    /* Pass 1: background quads (always, so the window is fully painted). An unselected block
       cursor paints its block here; a selected cursor cell keeps its own background (the band
       in pass 2 marks it). */
    for (int y = 0; y < g->rows; y++) {
        const Cell *row = grid_vprow(g, y);
        for (int x = 0; x < g->cols; x++) {
            const Cell *c = &row[x];
            uint32_t cellbg = c->bg;
            if (c->attr & ATTR_INVERSE)
                cellbg = c->fg;
            int is_cursor = t->show_cursor && g->scroll_off == g->live_off &&
                            x == t->cx && y == t->cy;
            if (is_cursor) {
                int hl_on = 0;
                if (sel_on) {
                    float h0, hw;
                    sel_band_pixels(&selspan, g->cols, x, y, cw, &h0, &hw);
                    /* must match pass 2 exactly: the block cursor is only hidden where a band is
                       actually painted, otherwise a selection covering the (empty) cursor cell would
                       erase the cursor without replacing it with a highlight */
                    hl_on = hw > 0.0f && term_cell_highlightable(t, x, y);
                }
                if (!hl_on)
                    cellbg = CURSOR_COL;
            }
            col2f(cellbg, bg);
            bg[3] *= r->opacity;
            float px = x * cw, py = (g->rows - 1 - y) * ch + pad;   /* GL y up, grid top-left aligned */
            emit_quad(&vp, px, py, cw, ch, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                      white, bg, 0.0f, 0.0f, 0.0f);
            nquads++;
        }
    }

    /* Pass 2: selection overlay bands (over the background, under the glyphs). Drawn for every
       covered cell that holds something, including the cursor cell, so the highlight always matches
       what term_selection_text() copies. hl0 is already an absolute pixel x — do NOT add px again. */
    if (sel_on) {
        for (int y = 0; y < g->rows; y++) {
            for (int x = 0; x < g->cols; x++) {
                /* No band over empty space. A cell that was never written (ch == 0)
                   has nothing to highlight, so a selection that reaches past the end of a line stops
                   at the last glyph instead of painting the whole window. A real space (' ') and the
                   continuation cell of a wide glyph DO get the band — see term_cell_highlightable. */
                if (!term_cell_highlightable(t, x, y))
                    continue;
                float hl0 = 0, hlw = 0;
                sel_band_pixels(&selspan, g->cols, x, y, cw, &hl0, &hlw);
                if (hlw <= 0.0f)
                    continue;
                float py = (g->rows - 1 - y) * ch + pad;
                emit_quad(&vp, hl0, py, hlw, ch,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, selc, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Pass 3: glyph quads (topmost). Continuation cells of a wide char are skipped (the wide
       glyph is drawn from its anchor cell). The glyph quad carries the cell's own background;
       the fragment shader picks the selection colour per-fragment where the glyph's pixel x
       falls inside [band0,band1), giving true sub-glyph selection without GL blending. */
    for (int y = 0; y < g->rows; y++) {
        const Cell *row = grid_vprow(g, y);
        for (int x = 0; x < g->cols; x++) {
            const Cell *c = &row[x];
            if (!c->ch || (c->attr & ATTR_WIDE))
                continue;

            /* Color emoji first: the cell's whole cluster (anchor + its continuation
               codepoints) is shaped by HarfBuzz into ONE color glyph. When the color font has
               nothing for that sequence we fall back to the mono glyph from the fontconfig
               fallback face (session 35), so emoji still draw on a box with no color font.
               seq/nseq stay in scope for the whole cell: the same sequence also decides the
               quad's span (cluster_is_wide below). */
            uint32_t seq[CLUSTER_MAX + 1];
            int nseq = 0;
            ColorGlyph *cg = NULL;
            if (f->color_ok && (c->nextra || cp_is_emoji(c->ch))) {
                nseq = 1;
                seq[0] = c->ch;
                for (int k = 0; k < (int)c->nextra && nseq <= CLUSTER_MAX; k++)
                    seq[nseq++] = c->extra[k];
                cg = font_color_glyph(f, seq, nseq);
            }
            Glyph *gl_ = NULL;
            if (!cg) {
                gl_ = font_glyph(f, c->ch);
                if (gl_->w <= 0 || gl_->h <= 0)
                    continue;
            }

            uint32_t cellbg = c->bg, cellfg = c->fg;
            if (c->attr & ATTR_INVERSE) {
                uint32_t tmp = cellbg;
                cellbg = cellfg;
                cellfg = tmp;
            }
            float hl0 = 0, hlw = 0;
            int hl_on = 0;
            if (sel_on) {
                sel_band_pixels(&selspan, g->cols, x, y, cw, &hl0, &hlw);
                hl_on = hlw > 0.0f;
            }
            float band0 = hl_on ? hl0 : 0.0f;
            float band1 = hl_on ? hl0 + hlw : 0.0f;
            int is_cursor = t->show_cursor && g->scroll_off == g->live_off &&
                            x == t->cx && y == t->cy;
            if (is_cursor) {
                uint32_t oldbg = cellbg;
                if (hl_on) {
                    cellfg = CURSOR_COL;   /* selected cursor: tint glyph, band already drawn */
                } else {
                    cellbg = CURSOR_COL;   /* unselected: glyph composites over the block */
                    cellfg = oldbg;        /* invert ink under the block cursor */
                }
            }
            col2f(cellfg, fg);
            col2f(cellbg, bg);
            /* The glyph quad composites the ink over ITS OWN copy of the cell background, so that
               background must carry the window opacity exactly like pass 1's quad does. It used to stay
               at alpha 1, which is what made every glyph's bounding box opaque. */
            bg[3] *= r->opacity;

            float px = x * cw, py = (g->rows - 1 - y) * ch + pad;

            if (cg) {
                /* The color bitmap carries UNSCALED strike metrics (128px strike on this box),
                   so the scale factor is cell-height / strike-height: zooming resizes the quad
                   without ever re-rasterizing the emoji. An emoji fills its cell box; a WIDE
                   emoji gets the 2-cell span it reserved, so it is never squished. */
                float s = ch / (float)f->color_px;
                float gw = (float)cg->w * s, gh = (float)cg->h * s;
                /* The span is the WHOLE cluster's width, not just the base codepoint's: vt.c
                   reserved 2 cells for a wide cluster, including clusters whose wide (emoji)
                   presentation only appears once a modifier is folded in (U+2764 U+FE0F, the
                   keycaps, a regional-indicator pair). cluster_is_wide is the same predicate
                   vt.c used, so the quad always matches the reservation. */
                float span = cluster_is_wide(seq, nseq) ? 2.0f * cw : cw;
                if (gw > span) {
                    float k = span / gw;
                    gw *= k;
                    gh *= k;
                }
                float gx = px + (span - gw) * 0.5f;   /* centered in the cell span */
                float gy = py + (ch - gh) * 0.5f;     /* centered in the line box */
                fg[0] = fg[1] = fg[2] = 1.0f; fg[3] = 1.0f;   /* color, never fg-tinted */
                if (c->attr & ATTR_DIM) {
                    fg[0] = fg[1] = fg[2] = 0.5f;     /* shader scales the premul texel */
                }
                emit_quad(&vp, gx, gy, gw, gh, cg->u0, cg->v0, cg->u1, cg->v1,
                          fg, bg, 2.0f, band0, band1);
                nquads++;
                continue;
            }

            float gw = (float)gl_->w * f->scale;
            float gh = (float)gl_->h * f->scale;
            float gx = px + (float)gl_->bearing_x * f->scale;
            /* baseline: cell_top=py+ch; baseline=cell_top-ascent;
               glyph_top=baseline+bearing_y*scale; quad_bottom=glyph_top-gh */
            float gy = py + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
            if (c->attr & ATTR_DIM) {
                fg[0] *= 0.5f; fg[1] *= 0.5f; fg[2] *= 0.5f;
            }
            emit_quad(&vp, gx, gy, gw, gh,
                      gl_->u0, gl_->v0, gl_->u1, gl_->v1, fg, bg, 1.0f,
                      band0, band1);
            nquads++;
        }
    }

    /* Upload BOTH atlases here, after the quads were emitted and before the draw. The glyph
       lookups (font_glyph / font_color_glyph) run inside pass 3, so a glyph shaped in THIS
       frame would otherwise be sampled before its texels exist: the old top-of-frame upload
       showed a newly rasterized glyph one frame late, and a first color emoji as a black box
       (the frame that shapes the emoji is often the last frame before the terminal idles). */
    /* upload atlas if new glyphs were added */
    if (f->atlas_dirty) {
        if (!f->atlas_tex)
            glGenTextures(1, &f->atlas_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
        /* GL_LUMINANCE (not GL_R8/GL_RED): the context is GL 2.1, where GL_R8 (a GL 3.0+
           internal format) and GL_RED (a legacy channel constant) are invalid and make
           glTexImage2D fail with GL_INVALID_ENUM. GL_LUMINANCE is valid GL 1.1+ and replicates
           the single channel to RGB, so the shader's texture2D(...).r works. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->atlas_w, f->atlas_h, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, f->atlas);
        GLenum terr = glGetError();
        if (terr != GL_NO_ERROR)
            klog(L_ERROR, "atlas glTexImage2D failed: 0x%x (%dx%d)", terr, f->atlas_w, f->atlas_h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->atlas_dirty = 0;
    }

    /* upload the RGBA8 color emoji atlas when new emoji were shaped into it (texture unit 1).
       GL_RGBA is a valid GL 2.1 internal format, so unlike the mono atlas this needs no
       legacy-format workaround. The texels are premultiplied (as FreeType hands them over),
       which is what the shader's `c + bg*(1-c.a)` composite expects. */
    if (f->catlas_dirty && f->catlas) {
        if (!f->catlas_tex)
            glGenTextures(1, &f->catlas_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, f->catlas_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f->catlas_w, f->catlas_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, f->catlas);
        GLenum cerr = glGetError();
        if (cerr != GL_NO_ERROR)
            klog(L_ERROR, "color atlas glTexImage2D failed: 0x%x (%dx%d)", cerr,
                 f->catlas_w, f->catlas_h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->catlas_dirty = 0;
    }

    glViewport(0, 0, r->vw, r->vh);
    glUseProgram(r->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
    glUniform1i(r->u_atlas, 0);
    if (f->catlas_tex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, f->catlas_tex);
        glUniform1i(r->u_catlas, 1);
    } else {
        /* no color glyph has ever been shaped: point the second sampler at the mono atlas so no
           texture unit is left unbound (no quad in this frame uses a_glyph == 2 anyway) */
        glUniform1i(r->u_catlas, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform4f(r->u_sel, selc[0], selc[1], selc[2], selc[3]);
    float mvp[16] = {
        2.0f / r->vw, 0, 0, 0,
        0, 2.0f / r->vh, 0, 0,
        0, 0, -1, 0,
        -1, -1, 0, 1,
    };
    glUniformMatrix4fv(r->u_mvp, 1, GL_FALSE, mvp);

    glBindVertexArray(r->vao);

    if (r->inst_ok) {
        fn_draw_arrays_instanced draw = (fn_draw_arrays_instanced)r->draw_inst;
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * FLOATS_PER_INST * sizeof(float),
                     vdata, GL_STREAM_DRAW);
        set_attribs_instanced(r);
        draw(GL_TRIANGLES, 0, 6, nquads);
    } else {
        float *edata = ensure_edata(r, g->rows, g->cols);
        expand_instances(vdata, nquads, edata);
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * 6 * FLOATS_PER_VERT * sizeof(float),
                     edata, GL_STREAM_DRAW);
        set_attribs_interleaved();
        glDrawArrays(GL_TRIANGLES, 0, nquads * 6);
    }

    /* Returns 1 when the buffer really was attached (eglSwapBuffers succeeded) and 0 otherwise.
       main.c needs that: a wl_surface_frame callback may only be armed together with a commit that
       attaches a buffer, so a failed swap must NOT arm one (the caller retries from its timer
       instead — see the frame-pacing invariant in main.c). */
    if (!eglSwapBuffers(r->edpy, r->esurf)) {
        klog(L_ERROR, "eglSwapBuffers failed: 0x%x", eglGetError());
        return 0;
    }
    return 1;
}

void render_resize(Render *r, int w, int h)
{
    /* On Wayland the EGL window surface tracks the wl_egl_window's buffer size
       automatically; wl_egl_window_resize() (main.c) already resized the native window.
       The surface must NOT be destroyed and re-created here (can return EGL_BAD_MATCH and
       leave esurf == EGL_NO_SURFACE, so nothing is ever painted). Just record the size. */
    if (r->edpy == EGL_NO_DISPLAY)
        return;
    r->vw = w;
    r->vh = h;
}

void render_destroy(Render *r)
{
    if (r->edpy != EGL_NO_DISPLAY) {
        eglMakeCurrent((EGLDisplay)r->edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->ectx != EGL_NO_CONTEXT)
            eglDestroyContext((EGLDisplay)r->edpy, (EGLContext)r->ectx);
        if (r->esurf != EGL_NO_SURFACE)
            eglDestroySurface((EGLDisplay)r->edpy, (EGLSurface)r->esurf);
        eglTerminate((EGLDisplay)r->edpy);
    }
    free(r->ecfg);
    free(r->vdata);
    free(r->edata);
    memset(r, 0, sizeof(*r));
}
