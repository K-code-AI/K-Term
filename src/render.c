/* Kai-Term — renderer: EGL + OpenGL 2.x, one textured-quad batch per frame.
 *
 * A frame is whatever src/layout.c says it is (see Layout in kai-term.h): the tab bar strip, then one grid
 * per pane (one pane in tab view, up to split_max panes side by side). Every rectangle carries its own
 * origin, so there is no global grid offset any more.
 *
 * Each visible cell emits:
 *   - a background quad (samples dummy white atlas pixel, color = cell bg)
 *   - a glyph quad: a mono glyph (alpha8 atlas, tinted by the cell fg) or a COLOR emoji
 *     (RGBA8 color atlas, premultiplied, not tinted) — see a_glyph in the fragment shader
 * The bar adds one background + one separator + one glyph per character per item, and side-by-side view
 * adds four quads for the focus border. A picture an application sent with the kitty graphics protocol
 * (see the graphics section of src/vt.c) adds one textured quad of its own.
 *
 * With the file manager on, the band src/layout.c put above the terminal pane(s) adds TWO more Cell
 * grids (the browser and the preview) drawn by the very same passes — fm.c paints ordinary Cells, so
 * there is no second drawing path — plus one textured quad for the preview picture and a border around
 * whichever region owns the keyboard.
 *
 * A frame is TWO vertex batches plus one draw per visible picture, not one draw call: this renderer never
 * enables GL blending (see fs_src), so the only way to put a picture over the cell backgrounds but under
 * the text -- which is how the protocol's own terminal draws it -- is to draw it in between.
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

#include "kai-term.h"

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
    "uniform sampler2D u_img;\n"        /* unit 2: the RGBA8 texture of one application picture */
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
       glyph, 3 = an application picture. A color texel is already premultiplied RGBA straight out of
       the emoji font, so it composites as texel + bg*(1-texel.a) and is deliberately NOT tinted by the
       text colour; v_fg is white (0.5 grey for ATTR_DIM) so dimming still works without a new
       attribute. 3 is the same composite against the texture of the picture being drawn -- vt.c
       premultiplies every decoded pixel, so no GL blending is needed there either. */
    "    if (v_glyph > 2.5) {\n"
    "        vec4 c = texture2D(u_img, v_tex);\n"
    "        gl_FragColor = vec4(c.rgb + bgp * (1.0 - c.a), c.a + bg.a * (1.0 - c.a));\n"
    "        return;\n"
    "    }\n"
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
    r->u_img = glGetUniformLocation(p, "u_img");
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

/* Clear colour = the configured background, premultiplied by the window opacity. A Wayland EGL ARGB
   buffer is premultiplied (the compositor computes surface_rgb + (1-surface_a)*backdrop), so the
   colour channels must already carry the alpha — the same rule the fragment shader follows. Used by
   render_init once and by render_draw every frame: the Config.padding margin and the whole-cell
   remainder at the bottom/right are never covered by a quad, and a resized buffer has no defined
   contents at all. */
static void set_clear_color(Render *r)
{
    float a = r->opacity;
    glClearColor(((r->bg >> 16) & 0xff) / 255.0f * a,
                 ((r->bg >> 8) & 0xff) / 255.0f * a,
                 (r->bg & 0xff) / 255.0f * a, a);
}

int render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                uint32_t bg, uint32_t fg)
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
    r->bg = bg;
    r->fg = fg;   /* the tab bar's text colour (see Render.fg) */
    r->ecfg = malloc(sizeof(cfg));
    if (r->ecfg)
        memcpy(r->ecfg, &cfg, sizeof(cfg));
    (void)win_id;

    klog(L_INFO, "GL: %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    r->prog = compile_prog(r);
    if (!r->prog)
        return -1;
    setup_gl(r);

    /* The buffer must start out as the configured background, not as DEF_BG: with Config.padding the
       margin around the grid is only ever painted by the clear, so a hardcoded DEF_BG showed up as a
       differently-coloured frame whenever `background` was set (measured: a `background = 0a0a2e`
       window had a 20 px (16,16,20) band on all four sides). Same premultiplication rule as the cell
       quads — see the comment in render_draw. */
    set_clear_color(r);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Do NOT swap here: attaching a buffer before the first xdg_surface.configure is
       acked is a Wayland protocol error. The first eglSwapBuffers happens in render_draw. */
    (void)win_id;
    return 0;
}

static float *ensure_vdata(Render *r, int max_quads)
{
    /* The caller counts the quads this frame can emit: at most 3 per grid cell (background + selection
       band + glyph) plus the chrome (one background and one separator per bar item, one glyph per label
       character, four for the focus border). */
    int need = max_quads * FLOATS_PER_INST;
    if (need > r->vcap) {
        free(r->vdata);
        r->vcap = need;
        r->vdata = malloc((size_t)need * sizeof(float));
        if (!r->vdata)
            abort();
    }
    return r->vdata;
}

static float *ensure_edata(Render *r, int max_quads)
{
    /* fallback expansion: 6 vertices per quad, FLOATS_PER_VERT floats per vertex */
    int need = max_quads * 6 * FLOATS_PER_VERT;
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

/* ---------- application pictures: one GL texture per model image ---------- */

#define IMAGE_TEX_MAX 512      /* textures held at once — more than any screen can show */
#define IMG_VIEW_MAX  64       /* pictures of one pane considered per frame */

/* The texture holding one image's texels: created on first use, re-uploaded when the model bumped
   `version` (an animation frame replaces the pixels). Keyed by KImage.serial, which the model never
   reuses, so a recycled KImage pointer can never make this cache hand back a stale texture. */
static unsigned img_tex(Render *r, KImage *im, int frame)
{
    ImgTex *sl = NULL;
    for (int i = 0; i < r->nimgtex; i++)
        if (r->imgtex[i].serial == im->serial) {
            sl = &r->imgtex[i];
            break;
        }
    if (!sl) {
        if (r->nimgtex >= r->imgtex_cap) {
            int cap = r->imgtex_cap ? r->imgtex_cap * 2 : 16;
            if (cap > IMAGE_TEX_MAX)
                cap = IMAGE_TEX_MAX;
            if (r->nimgtex >= cap)
                return 0;
            ImgTex *nt = realloc(r->imgtex, (size_t)cap * sizeof *nt);
            if (!nt)
                return 0;
            r->imgtex = nt;
            r->imgtex_cap = cap;
        }
        sl = &r->imgtex[r->nimgtex++];
        memset(sl, 0, sizeof *sl);
        sl->serial = im->serial;
        GLuint t = 0;
        glGenTextures(1, &t);
        sl->tex = t;
    }
    sl->seen = frame;
    if (sl->version != im->version || sl->w != im->w || sl->h != im->h) {
        glBindTexture(GL_TEXTURE_2D, sl->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im->w, im->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, im->rgba);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
            klog(L_WARN, "image glTexImage2D failed: 0x%x (%dx%d)", err, im->w, im->h);
        /* LINEAR, not NEAREST: a picture is scaled from its own pixel grid to the cell grid, and
           nearest turns that into a mosaic. No mipmaps, so a non-power-of-two size is legal on a
           GL 2.1 context with CLAMP_TO_EDGE. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        sl->version = im->version;
        sl->w = im->w;
        sl->h = im->h;
    }
    return sl->tex;
}

/* Drop the textures of pictures that were not on screen this frame, so the GPU never holds more than
   what the user can see and an image the model evicted loses its texture in the same frame. */
static void img_tex_sweep(Render *r, int frame)
{
    int keep = 0;
    for (int i = 0; i < r->nimgtex; i++) {
        if (r->imgtex[i].seen == frame) {
            if (keep != i)
                r->imgtex[keep] = r->imgtex[i];
            keep++;
        } else if (r->imgtex[i].tex) {
            GLuint t = r->imgtex[i].tex;
            glDeleteTextures(1, &t);
        }
    }
    r->nimgtex = keep;
}

/* Upload one batch of instance records and draw it. Reusing r->vbo per batch is safe: glBufferData
   copies the staging data into driver storage before the next batch overwrites it. */
static void draw_quads(Render *r, const float *inst, int nquads)
{
    if (nquads <= 0)
        return;
    if (r->inst_ok) {
        fn_draw_arrays_instanced draw = (fn_draw_arrays_instanced)r->draw_inst;
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * FLOATS_PER_INST * sizeof(float),
                     inst, GL_STREAM_DRAW);
        set_attribs_instanced(r);
        draw(GL_TRIANGLES, 0, 6, nquads);
    } else {
        float *edata = ensure_edata(r, nquads);
        expand_instances(inst, nquads, edata);
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * 6 * FLOATS_PER_VERT * sizeof(float),
                     edata, GL_STREAM_DRAW);
        set_attribs_interleaved();
        glDrawArrays(GL_TRIANGLES, 0, nquads * 6);
    }
}

/* The GL state every batch shares. Called before each batch: an atlas texture can be created between
   batches (the glyph lookups run while the quads are emitted) and the picture sampler must point at a
   bound texture when nothing is on screen. */
static void setup_draw_state(Render *r, const Font *f, const float *selc)
{
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
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
    glUniform1i(r->u_img, 2);
    glActiveTexture(GL_TEXTURE0);
    glUniform4f(r->u_sel, selc[0], selc[1], selc[2], selc[3]);
    float mvp[16] = {
        2.0f / r->vw, 0, 0, 0,
        0, 2.0f / r->vh, 0, 0,
        0, 0, -1, 0,
        -1, -1, 0, 1,
    };
    glUniformMatrix4fv(r->u_mvp, 1, GL_FALSE, mvp);
}

/* ---------- the frame: the tab bar plus one grid per pane ---------- */

/* Blend two ARGB colours, k100 = percent of b (0..100). Only the CHROME uses this: the cells keep their
   own colours untouched, so an application that repaints its palette still cannot repaint the bar. */
static uint32_t mix_c(uint32_t a, uint32_t b, int k100)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int av = (int)((a >> sh) & 0xff), bv = (int)((b >> sh) & 0xff);
        int v = av + (bv - av) * k100 / 100;
        out |= (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)) << sh;
    }
    return out;
}

/* Decode one UTF-8 sequence of a tab-bar label. A tab's title arrives through OSC 0/2, so a label is
   arbitrary UTF-8; a stray continuation byte becomes U+FFFD, which the font draws as its notdef box. */
static uint32_t next_cp(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    uint32_t cp = *s++;
    int need = 0;
    if (cp >= 0xf0) { cp &= 0x07u; need = 3; }
    else if (cp >= 0xe0) { cp &= 0x0fu; need = 2; }
    else if (cp >= 0xc0) { cp &= 0x1fu; need = 1; }
    else if (cp >= 0x80) { *pp = (const char *)s; return 0xfffd; }
    for (int i = 0; i < need; i++) {
        if (s[0] < 0x80 || s[0] >= 0xc0)
            break;
        cp = (cp << 6) | (uint32_t)(s[0] & 0x3f);
        s++;
    }
    *pp = (const char *)s;
    return cp;
}

/* The glyph quad of one cell: the color emoji the color font has for the whole cluster, or the mono
   glyph. Returns 1 when a quad was emitted. This is the code pass 3 used to carry inline; it is a
   function now because the file-manager panes need exactly the same glyph handling (their cells can
   hold any codepoint a file name contains, including emoji) and a second copy of the emoji/mono/wide
   rules is how the two would drift apart.
   fg/bg are already resolved by the caller (inverse, cursor, selection) and bg[3] already carries the
   window opacity; ATTR_DIM is applied here so both callers dim identically. band0/band1 are the
   selection band's absolute pixel edges (0 when there is none — the file manager has no selection). */
static int emit_cell_glyph(Font *f, const Cell *c, float px, float py, float cw, float ch,
                           float ascent, const float *fg, const float *bg,
                           float band0, float band1, float **vp)
{
    /* Color emoji first: the cell's whole cluster (anchor + its continuation codepoints) is
       shaped by HarfBuzz into ONE color glyph. When the color font has nothing for that
       sequence we fall back to the mono glyph from the fontconfig fallback face (session 35),
       so emoji still draw on a box with no color font. seq/nseq stay in scope for the whole
       cell: the same sequence also decides the quad's span (cluster_is_wide below). */
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
        if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
            return 0;
    }

    float cf[4];
    cf[0] = fg[0]; cf[1] = fg[1]; cf[2] = fg[2]; cf[3] = fg[3];

    if (cg) {
        /* The color bitmap carries UNSCALED strike metrics, and f->color_px is that
           strike's own pixel height as the font reports it (128 for the installed Noto
           Color Emoji), so the scale factor is cell-height / strike-height: zooming
           resizes the quad without ever re-rasterizing the emoji. An emoji fills its
           cell box; a WIDE emoji gets the 2-cell span it reserved, so it is never
           squished. */
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
        cf[0] = cf[1] = cf[2] = 1.0f; cf[3] = 1.0f;   /* color, never fg-tinted */
        if (c->attr & ATTR_DIM)
            cf[0] = cf[1] = cf[2] = 0.5f;     /* shader scales the premul texel */
        emit_quad(vp, gx, gy, gw, gh, cg->u0, cg->v0, cg->u1, cg->v1, cf, bg, 2.0f,
                  band0, band1);
        return 1;
    }

    float gw = (float)gl_->w * f->scale;
    float gh = (float)gl_->h * f->scale;
    float gx = px + (float)gl_->bearing_x * f->scale;
    /* baseline: cell_top=py+ch; baseline=cell_top-ascent;
       glyph_top=baseline+bearing_y*scale; quad_bottom=glyph_top-gh */
    float gy = py + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
    if (c->attr & ATTR_DIM) {
        cf[0] *= 0.5f; cf[1] *= 0.5f; cf[2] *= 0.5f;
    }
    emit_quad(vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1, cf, bg, 1.0f,
              band0, band1);
    return 1;
}

/* A rectangular border `t` px thick, drawn just OUTSIDE the rect (in the gap around it), so it never
   covers a glyph. Used for the side-by-side focus border and for the file-manager focus border. */
static void emit_border(float **vp, float x, float y, float w, float h, float t,
                        const float *bg, float du, float dv, float vh)
{
    const float white[4] = { 1, 1, 1, 1 };
    float gl_bot = vh - (y + h), gl_top = vh - y;
    emit_quad(vp, x - t, gl_top, w + 2.0f * t, t, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x - t, gl_bot - t, w + 2.0f * t, t, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x - t, gl_bot, t, h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x + w, gl_bot, t, h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
}

/* Everything one pane needs in all three passes, computed once per pane per frame. */
typedef struct {
    Term *t;
    Grid *g;
    const PaneGeom *pg;
    int nrows, ncols;    /* the grid, clamped to what the pane can hold */
    float gx;            /* the grid's left edge in buffer px (== the pane's x: no inner margin) */
    float pad_gl;        /* GL y of the bottom of the last drawn row */
    int sel_on;
    SelSpan selspan;
} PaneDraw;

static void pane_prepare(Render *r, Term *t, const PaneGeom *pg, Font *f, PaneDraw *pd)
{
    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    pd->t = t;
    pd->g = &t->g;
    pd->pg = pg;
    /* Never draw more than the pane holds. main.c resizes every displayed session to its pane, so this
       only bites on a grid that has not caught up with a layout change yet — and then it stops the
       drawing from spilling into the neighbouring pane. */
    int cwi = imax_((int)(cw + 0.5f), 1), chi = imax_((int)(ch + 0.5f), 1);
    pd->nrows = iclamp_(pd->g->rows, 0, pg->h / chi);
    pd->ncols = iclamp_(pd->g->cols, 0, pg->w / cwi);
    pd->gx = (float)pg->x;
    /* Window y grows down while GL y grows up, and the grid's TOP row starts at the pane's top edge, so
       the pixels that do not fill a whole row fall BELOW the last row and those that do not fill a whole
       column fall to the RIGHT. That is the same remainder rule the pointer mapping depends on
       (term_grid_geometry), now applied inside each pane instead of inside the window: viewport row y
       occupies window pixels [pg->y + y*ch, pg->y + (y+1)*ch) and column x occupies
       [pg->x + x*cw, pg->x + (x+1)*cw), which is exactly what main.c's sel_cell()/mouse_cell() compute. */
    pd->pad_gl = (float)r->vh - (float)pg->y - (float)pd->nrows * ch;
    if (pd->pad_gl < 0.0f)
        pd->pad_gl = 0.0f;
    pd->sel_on = t->sel_active;
    if (pd->sel_on)
        term_selection_span(t, &pd->selspan);
}

/* The session a pane shows. `terms` is indexed by TAB index (see the render_draw comment in kai-term.h). */
static Term *pane_term(Term *const *terms, int tab)
{
    if (!terms || tab < 0 || tab >= TAB_MAX)
        return NULL;
    return terms[tab];
}

/* Draw every picture of one pane that is at least partly inside it. The model places pictures in CELLS
   (viewport row/column + span) while the protocol sizes them in PIXELS, so the rect is built in window
   px and then clipped to the pane: without the clip a picture whose anchor row scrolled above the pane
   top would be painted over the tab bar or into the neighbouring pane, and the texture coordinates of
   the part that IS visible would be wrong. */
static int draw_images(Render *r, Term *t, const PaneGeom *pg, const PaneDraw *pd,
                       float cw, float ch, int frame)
{
    ImgView view[IMG_VIEW_MAX];
    int n = term_visible_images(t, view, IMG_VIEW_MAX);
    if (n <= 0 || pd->nrows <= 0 || pd->ncols <= 0)
        return 0;

    const float white[4] = { 1, 1, 1, 1 };
    int drawn = 0;

    for (int i = 0; i < n; i++) {
        KImage *im = view[i].img;
        if (!im->rgba || im->w <= 0 || im->h <= 0)
            continue;
        unsigned tex = img_tex(r, im, frame);
        if (!tex)
            continue;

        /* An explicit c=/r= scales the picture into exactly that cell area (vt.c derives the missing
           dimension from the picture's own aspect ratio); with neither, the protocol draws it at its
           native pixel size and the ceil()ed span only says how many cells it covers. */
        float w_px, h_px;
        if (im->cols > 0 || im->rows > 0) {
            w_px = (float)view[i].ccols * cw;
            h_px = (float)view[i].crows * ch;
        } else {
            w_px = (float)im->w;
            h_px = (float)im->h;
        }
        if (w_px <= 0.0f || h_px <= 0.0f)
            continue;

        float x0 = pd->gx + (float)view[i].ccol * cw + (float)im->px;
        float y0 = (float)pg->y + (float)view[i].crow * ch + (float)im->py;
        float cl = x0, ct = y0, cr = x0 + w_px, cb = y0 + h_px;
        if (cl < (float)pg->x) cl = (float)pg->x;
        if (ct < (float)pg->y) ct = (float)pg->y;
        if (cr > (float)(pg->x + pg->w)) cr = (float)(pg->x + pg->w);
        if (cb > (float)(pg->y + pg->h)) cb = (float)(pg->y + pg->h);
        if (cr <= cl || cb <= ct)
            continue;

        /* the visible sub-rectangle of the texture: v grows down through the image rows, exactly the
           atlas convention emit_quad expects (it Y-flips for GL's bottom-up y) */
        float u0 = (cl - x0) / w_px, u1 = (cr - x0) / w_px;
        float v0 = (ct - y0) / h_px, v1 = (cb - y0) / h_px;

        /* No GL blending, so the quad composites against its OWN copy of the background behind it: the
           colour of the cell its top-left visible corner sits in, which is what batch A already painted
           there. A picture spanning cells with different backgrounds blends against only one of them —
           the price of never enabling blending (see fs_src). */
        const Cell *anchor = &grid_vprow(pd->g, iclamp_(view[i].crow, 0, pd->nrows - 1))
                                [iclamp_(view[i].ccol, 0, pd->ncols - 1)];
        uint32_t cellbg = (anchor->attr & ATTR_INVERSE) ? anchor->fg : anchor->bg;
        float bg[4];
        col2f(cellbg, bg);
        bg[3] *= r->opacity;

        float rec[FLOATS_PER_INST];
        float *ip = rec;
        emit_quad(&ip, cl, (float)r->vh - cb, cr - cl, cb - ct,
                  u0, v0, u1, v1, white, bg, 3.0f, 0.0f, 0.0f);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex);
        draw_quads(r, rec, 1);
        drawn++;
    }
    glActiveTexture(GL_TEXTURE0);
    return drawn;
}

/* ---------- the file-manager band ---------- */

/* One of the two band panes, in the shape the passes want. which: 0 = the browser, 1 = the preview.
   Returns 0 when that pane is not part of this frame (band off, preview toggled away). */
static int fm_pane_rect(const FmGeom *fg, int which, int *x, int *y, int *w, int *h)
{
    if (!fg->on)
        return 0;
    if (which == 0) {
        *x = fg->list_x; *y = fg->list_y; *w = fg->list_w; *h = fg->list_h;
        return fg->list_cols > 0 && fg->list_rows > 0;
    }
    if (!fg->prev_on)
        return 0;
    *x = fg->prev_x; *y = fg->prev_y; *w = fg->prev_w; *h = fg->prev_h;
    return fg->prev_cols > 0 && fg->prev_rows > 0;
}

typedef struct {
    const Cell *cell;
    int stride;          /* the grid's own column count, which is what indexes cell[] */
    int nrows, ncols;    /* clamped to what the rect can hold, exactly like pane_prepare() */
    float gx;
    float pad_gl;
} FmPaneDraw;

static void fm_pane_prepare(const Render *r, const FmPane *p, int x, int y, int w, int h,
                            Font *f, FmPaneDraw *fp)
{
    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    int cwi = imax_((int)(cw + 0.5f), 1), chi = imax_((int)(ch + 0.5f), 1);
    fp->cell = p->cell;
    fp->stride = p->cols;
    fp->nrows = p->cell ? iclamp_(p->rows, 0, h / chi) : 0;
    fp->ncols = p->cell ? iclamp_(p->cols, 0, w / cwi) : 0;
    fp->gx = (float)x;
    /* Same remainder rule as a session pane: the top row starts at the rect's top edge, so the pixels
       that do not fill a whole row fall BELOW it and the GL y of the last row's bottom is
       vh - y - nrows*ch. fm_set_size() sized the grid from term_grid_geometry() on this very rect. */
    fp->pad_gl = (float)r->vh - (float)y - (float)fp->nrows * ch;
    if (fp->pad_gl < 0.0f)
        fp->pad_gl = 0.0f;
}

/* The preview picture's texture. At most one preview exists at a time, so this is one texture instead
   of a cache: it is re-uploaded when FmPreview.uid changes (a new preview was installed) or when the
   pixels changed size. uid is never reused, so a recycled malloc address can never paint a stale
   picture — the same rule KImage.serial enforces for application pictures.
   fm.c hands out plain RGBA (its tests compare decoded bytes), and the shader composites premultiplied
   texels, so the copy in Render.fm_px is what gets uploaded. */
static unsigned fm_preview_tex(Render *r, const FmPreview *pv)
{
    if (!pv || pv->kind != FMP_IMAGE || !pv->rgba || pv->w <= 0 || pv->h <= 0)
        return 0;
    if (r->fm_tex_uid == pv->uid && r->fm_tex_w == pv->w && r->fm_tex_h == pv->h)
        return r->fm_tex;

    size_t need = (size_t)pv->w * (size_t)pv->h * 4u;
    if ((size_t)r->fm_px_cap < need) {
        free(r->fm_px);
        uint8_t *buf = malloc(need);
        if (!buf) {
            r->fm_px = NULL;
            r->fm_px_cap = 0;
            return 0;
        }
        r->fm_px = buf;
        r->fm_px_cap = (int)need;
    }
    memcpy(r->fm_px, pv->rgba, need);
    img_premultiply(r->fm_px, (long long)pv->w * pv->h);

    if (!r->fm_tex)
        glGenTextures(1, &r->fm_tex);
    glBindTexture(GL_TEXTURE_2D, r->fm_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pv->w, pv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, r->fm_px);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        klog(L_WARN, "fm preview glTexImage2D failed: 0x%x (%dx%d)", err, pv->w, pv->h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    r->fm_tex_uid = pv->uid;
    r->fm_tex_w = pv->w;
    r->fm_tex_h = pv->h;
    return r->fm_tex;
}

/* Draw the preview picture, clipped to the preview rect. fm_image_fit() decided the box (aspect kept in
   PIXELS, never upscaled, centred, returned in cells AND in px); the cell box is where it goes and the
   px box is what is drawn inside it, so the ceil()-ing into cells cannot stretch the picture. */
static int draw_fm_preview(Render *r, const FileMgr *fm, const Layout *lay, float cw, float ch)
{
    const FmGeom *fgm = &lay->fm;
    if (!fm || !fgm->on || !fgm->prev_on || !fm->pv || fm->pv->kind != FMP_IMAGE)
        return 0;
    if (!fm->prev.cell || fm->prev.cols <= 0 || fm->prev.rows <= 0)
        return 0;

    int col, row, cols, rows, px_w, px_h;
    fm_image_fit(fm, imax_((int)(cw + 0.5f), 1), imax_((int)(ch + 0.5f), 1),
                 &col, &row, &cols, &rows, &px_w, &px_h);
    if (cols <= 0 || rows <= 0 || px_w <= 0 || px_h <= 0)
        return 0;

    unsigned tex = fm_preview_tex(r, fm->pv);
    if (!tex)
        return 0;

    /* the cell box, then the picture centred inside it */
    float bx = (float)fgm->prev_x + (float)col * cw;
    float by = (float)fgm->prev_y + (float)row * ch;
    float x0 = bx + ((float)cols * cw - (float)px_w) * 0.5f;
    float y0 = by + ((float)rows * ch - (float)px_h) * 0.5f;
    float cl = x0, ct = y0, cr = x0 + (float)px_w, cb = y0 + (float)px_h;
    if (cl < (float)fgm->prev_x) cl = (float)fgm->prev_x;
    if (ct < (float)fgm->prev_y) ct = (float)fgm->prev_y;
    if (cr > (float)(fgm->prev_x + fgm->prev_w)) cr = (float)(fgm->prev_x + fgm->prev_w);
    if (cb > (float)(fgm->prev_y + fgm->prev_h)) cb = (float)(fgm->prev_y + fgm->prev_h);
    if (cr <= cl || cb <= ct)
        return 0;

    float u0 = (cl - x0) / (float)px_w, u1 = (cr - x0) / (float)px_w;
    float v0 = (ct - y0) / (float)px_h, v1 = (cb - y0) / (float)px_h;

    /* No GL blending, so the quad composites against its own copy of the background under it: the
       preview pane's cell there, which pass 1 already painted. fm.c leaves the cells under a picture
       blank on purpose so the picture sits on the pane background. */
    int crow = iclamp_(row, 0, fm->prev.rows - 1);
    int ccol = iclamp_(col, 0, fm->prev.cols - 1);
    const Cell *anchor = &fm->prev.cell[(size_t)crow * (size_t)fm->prev.cols + (size_t)ccol];
    float bg[4];
    col2f(anchor->bg, bg);
    bg[3] *= r->opacity;

    const float white[4] = { 1, 1, 1, 1 };
    float rec[FLOATS_PER_INST];
    float *ip = rec;
    emit_quad(&ip, cl, (float)r->vh - cb, cr - cl, cb - ct, u0, v0, u1, v1, white, bg, 3.0f,
              0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex);
    draw_quads(r, rec, 1);
    glActiveTexture(GL_TEXTURE0);
    return 1;
}

int render_draw(Render *r, Font *f, const Layout *lay, Term *const *terms, const FileMgr *fm)
{
    glClear(GL_COLOR_BUFFER_BIT);

    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    float ascent = (float)f->ascent * f->scale;
    const float white[4] = { 1, 1, 1, 1 };
    const float du = 1.0f / (float)f->atlas_w, dv = 1.0f / (float)f->atlas_h;

    /* Window transparency (Config.opacity): the cell/selection/cursor and chrome colours are opaque
       ARGB, so the alpha is scaled here and carried to the shader in a_bg.a. Ink keeps alpha 1 (see
       fs_src), only the background becomes see-through. opacity == 1 leaves every value untouched. */
    float selc[4];
    col2f(SEL_BG, selc);
    selc[3] *= r->opacity;

    /* Chrome colours, mixed from the configured palette (Render.bg/Render.fg): the visible session's bar
       item uses the selection highlight, the others a background nudged toward the foreground with dimmed
       text, and the cursor of a session that is DRAWN but not the visible one is dimmed too — so a
       side-by-side window still shows which session the keyboard talks to. */
    uint32_t item_off_bg = mix_c(r->bg, r->fg, 10);
    uint32_t item_off_fg = mix_c(r->fg, r->bg, 45);
    uint32_t item_on_bg  = SEL_BG;
    uint32_t sep_col     = mix_c(r->fg, r->bg, 65);
    uint32_t cur_off     = mix_c(CURSOR_COL, r->bg, 55);
    uint32_t border_col  = SEL_BG;

    /* Staging space: at most 3 quads per grid cell (background + band + glyph), at most
       (BAR_LABEL_MAX + 2) per bar item (label glyphs + background + separator), and 4 for the border. */
    int cells = 0;
    for (int p = 0; p < lay->npane; p++)
        cells += lay->pane[p].cols * lay->pane[p].rows;
    int fmcells = 0;
    if (fm && fm->on) {
        fmcells += lay->fm.list_cols * lay->fm.list_rows;
        if (lay->fm.prev_on)
            fmcells += lay->fm.prev_cols * lay->fm.prev_rows;
    }
    /* +8: the focus border is 4 quads, and with the file manager on a pane border and a band border can
       both be in the same frame. */
    int maxq = 3 * cells + 3 * fmcells + lay->nitem * (BAR_LABEL_MAX + 2) + 8;
    float *vdata = ensure_vdata(r, maxq);
    float *vp = vdata;
    int nquads = 0;
    float bg[4], fg[4];

    /* Three passes, back to front, over EVERY pane: (1) backgrounds, (2) selection bands, (3) glyphs.
       All backgrounds of all panes go before any glyph because a WIDE glyph's quad spans into the next
       cell — in side-by-side mode that also keeps a pane's glyph from being clipped by its neighbour's
       background. The chrome is appended at the end of the pass it belongs to, so it sits on top of the
       grid it borders. There is no GL blending: within one draw call the later quad simply wins, which is
       Passes 1 and 2+3 are TWO draw calls with the pictures drawn between them, because a picture has
       to be over the cell background but under the text — that is how the protocol's own terminal draws
       it, and with no GL blending the draw order is the only thing that can achieve it. Both batches
       live in ONE staging buffer: batch A is already in driver storage when batch B is built, so the
       split is just two ranges of the same records. */

    /* Pass 1: cell backgrounds (always, so every pane is fully painted). An unselected block cursor
       paints its block here; a selected cursor cell keeps its own background (the band in pass 2 marks
       it). */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        uint32_t cur_col = (lay->pane[p].tab == lay->active_tab) ? CURSOR_COL : cur_off;
        for (int y = 0; y < pd.nrows; y++) {
            const Cell *row = grid_vprow(pd.g, y);
            for (int x = 0; x < pd.ncols; x++) {
                const Cell *c = &row[x];
                uint32_t cellbg = c->bg;
                if (c->attr & ATTR_INVERSE)
                    cellbg = c->fg;
                int is_cursor = t->show_cursor && pd.g->scroll_off == pd.g->live_off &&
                                x == t->cx && y == t->cy;
                if (is_cursor) {
                    int hl_on = 0;
                    if (pd.sel_on) {
                        float h0, hw;
                        sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &h0, &hw);
                        /* must match pass 2 exactly: the block cursor is only hidden where a band is
                           actually painted, otherwise a selection covering the (empty) cursor cell would
                           erase the cursor without replacing it with a highlight */
                        hl_on = hw > 0.0f && term_cell_highlightable(t, x, y);
                    }
                    if (!hl_on)
                        cellbg = cur_col;
                }
                col2f(cellbg, bg);
                bg[3] *= r->opacity;
                float px = pd.gx + x * cw, py = (pd.nrows - 1 - y) * ch + pd.pad_gl;
                emit_quad(&vp, px, py, cw, ch, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, bg, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* The file-manager band, pass 1: cell backgrounds. fm.c painted these grids with the CONFIGURED
       fg/bg plus its own accents, so the band sits inside the terminal's palette. No cursor and no
       selection live here, which is all that distinguishes a band pane from a session pane. */
    if (fm && fm->on) {
        for (int q = 0; q < 2; q++) {
            int x, y, w, h;
            if (!fm_pane_rect(&lay->fm, q, &x, &y, &w, &h))
                continue;
            FmPaneDraw fp;
            fm_pane_prepare(r, q ? &fm->prev : &fm->list, x, y, w, h, f, &fp);
            for (int yy = 0; yy < fp.nrows; yy++) {
                const Cell *row = fp.cell + (size_t)yy * (size_t)fp.stride;
                for (int xx = 0; xx < fp.ncols; xx++) {
                    const Cell *c = &row[xx];
                    uint32_t cellbg = (c->attr & ATTR_INVERSE) ? c->fg : c->bg;
                    col2f(cellbg, bg);
                    bg[3] *= r->opacity;
                    float px = fp.gx + xx * cw, py = (fp.nrows - 1 - yy) * ch + fp.pad_gl;
                    emit_quad(&vp, px, py, cw, ch, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                              white, bg, 0.0f, 0.0f, 0.0f);
                    nquads++;
                }
            }
        }
    }

    /* The bar: one background per item (the strip itself is the clear colour), and a 1 px separator at
       the left edge of every item but the first so two inactive items do not merge into one block. */
    float bar_gl = 0.0f;
    if (lay->bar_on) {
        bar_gl = (float)r->vh - (float)lay->bar_y - (float)lay->bar_h;
        for (int i = 0; i < lay->nitem; i++) {
            const BarItem *it = &lay->item[i];
            col2f(it->active ? item_on_bg : item_off_bg, bg);
            bg[3] *= r->opacity;
            emit_quad(&vp, (float)it->x, bar_gl, (float)it->w, (float)lay->bar_h,
                      du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
            nquads++;
            if (i > 0) {
                col2f(sep_col, bg);
                bg[3] *= r->opacity;
                emit_quad(&vp, (float)it->x, bar_gl, 1.0f, (float)lay->bar_h,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Everything emitted so far is batch A: cell backgrounds, the block cursor, the bar item
       backgrounds and separators. The pictures go on top of exactly these, and everything after this
       point (bands, glyphs, labels, border) goes on top of the pictures. */
    int nq_bg = nquads;

    /* Pass 2: selection overlay bands (over the background, under the glyphs), per pane. Drawn for every
       covered cell that holds something, including the cursor cell, so the highlight always matches what
       term_selection_text() copies. hl0 is already an absolute pixel x — do NOT add px again.
       The band edges are absolute buffer px and panes never overlap in x, so a band can never tint a
       glyph of a different pane (the shader compares the fragment's absolute x). */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t || !t->sel_active)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        for (int y = 0; y < pd.nrows; y++) {
            for (int x = 0; x < pd.ncols; x++) {
                /* No band over empty space. A cell that was never written (ch == 0) has nothing to
                   highlight, so a selection that reaches past the end of a line stops at the last glyph
                   instead of painting the whole window. A real space (' ') and the continuation cell of a
                   wide glyph DO get the band — see term_cell_highlightable. */
                if (!term_cell_highlightable(t, x, y))
                    continue;
                float hl0 = 0, hlw = 0;
                sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &hl0, &hlw);
                if (hlw <= 0.0f)
                    continue;
                float py = (pd.nrows - 1 - y) * ch + pd.pad_gl;
                emit_quad(&vp, pd.gx + hl0, py, hlw, ch,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, selc, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Pass 3: glyph quads (topmost), per pane. Continuation cells of a wide char are skipped (the wide
       glyph is drawn from its anchor cell). The glyph quad carries the cell's own background; the
       fragment shader picks the selection colour per-fragment where the glyph's pixel x falls inside
       [band0,band1), giving true sub-glyph selection without GL blending. */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        for (int y = 0; y < pd.nrows; y++) {
            const Cell *row = grid_vprow(pd.g, y);
            for (int x = 0; x < pd.ncols; x++) {
                const Cell *c = &row[x];
                if (!c->ch || (c->attr & ATTR_WIDE))
                    continue;

                uint32_t cellbg = c->bg, cellfg = c->fg;
                if (c->attr & ATTR_INVERSE) {
                    uint32_t tmp = cellbg;
                    cellbg = cellfg;
                    cellfg = tmp;
                }
                float hl0 = 0, hlw = 0;
                int hl_on = 0;
                if (pd.sel_on) {
                    sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &hl0, &hlw);
                    hl_on = hlw > 0.0f;
                }
                /* The shader compares the fragment's ABSOLUTE buffer pixel x with the band edges, so the
                   band must carry the pane's grid origin exactly like the quad origin below does. */
                float band0 = hl_on ? pd.gx + hl0 : 0.0f;
                float band1 = hl_on ? pd.gx + hl0 + hlw : 0.0f;
                int is_cursor = t->show_cursor && pd.g->scroll_off == pd.g->live_off &&
                                x == t->cx && y == t->cy;
                uint32_t cur_col = (lay->pane[p].tab == lay->active_tab) ? CURSOR_COL : cur_off;
                if (is_cursor) {
                    uint32_t oldbg = cellbg;
                    if (hl_on) {
                        cellfg = cur_col;      /* selected cursor: tint glyph, band already drawn */
                    } else {
                        cellbg = cur_col;      /* unselected: glyph composites over the block */
                        cellfg = oldbg;        /* invert ink under the block cursor */
                    }
                }
                col2f(cellfg, fg);
                col2f(cellbg, bg);
                /* The glyph quad composites the ink over ITS OWN copy of the cell background, so that
                   background must carry the window opacity exactly like pass 1's quad does. It used to stay
                   at alpha 1, which is what made every glyph's bounding box opaque. */
                bg[3] *= r->opacity;

                float px = pd.gx + x * cw, py = (pd.nrows - 1 - y) * ch + pd.pad_gl;

                if (emit_cell_glyph(f, c, px, py, cw, ch, ascent, fg, bg, band0, band1, &vp))
                    nquads++;
            }
        }
    }

    /* The file-manager band, pass 3: glyphs. Same helper, same font, same emoji rules — a file name can
       contain any codepoint, and fm.c already gave a wide one the ATTR_WIDE continuation cell vt.c
       uses, so the wide/cluster logic above applies to it unchanged. No selection band here (the file
       manager has no text selection), hence band0 == band1 == 0. */
    if (fm && fm->on) {
        for (int q = 0; q < 2; q++) {
            int x, y, w, h;
            if (!fm_pane_rect(&lay->fm, q, &x, &y, &w, &h))
                continue;
            FmPaneDraw fp;
            fm_pane_prepare(r, q ? &fm->prev : &fm->list, x, y, w, h, f, &fp);
            for (int yy = 0; yy < fp.nrows; yy++) {
                const Cell *row = fp.cell + (size_t)yy * (size_t)fp.stride;
                for (int xx = 0; xx < fp.ncols; xx++) {
                    const Cell *c = &row[xx];
                    if (!c->ch || (c->attr & ATTR_WIDE))
                        continue;
                    uint32_t cbg = c->bg, cfg = c->fg;
                    if (c->attr & ATTR_INVERSE) {   /* same swap pass 1 applies, so an inverted cell
                                                       cannot get ink over ink */
                        uint32_t tmp = cbg;
                        cbg = cfg;
                        cfg = tmp;
                    }
                    col2f(cfg, fg);
                    col2f(cbg, bg);
                    bg[3] *= r->opacity;
                    float px = fp.gx + xx * cw, py = (fp.nrows - 1 - yy) * ch + fp.pad_gl;
                    if (emit_cell_glyph(f, c, px, py, cw, ch, ascent, fg, bg, 0.0f, 0.0f, &vp))
                        nquads++;
                }
            }
        }
    }

    /* The bar labels, drawn with the text font over the item backgrounds. A label never crosses its own
       item rect, so a long tab title is cut (layout_compute already cut it to the item's columns; this
       bound is what makes that guarantee hold even if the font is wider than the layout assumed). */
    if (lay->bar_on) {
        for (int i = 0; i < lay->nitem; i++) {
            const BarItem *it = &lay->item[i];
            col2f(it->active ? r->fg : item_off_fg, fg);
            col2f(it->active ? item_on_bg : item_off_bg, bg);
            bg[3] *= r->opacity;
            float px0 = (float)it->x;
            int col = 0;
            for (const char *s = it->label; *s; col++) {
                uint32_t cp = next_cp(&s);
                if (px0 + (float)(col + 1) * cw > (float)(it->x + it->w))
                    break;
                Glyph *gl_ = font_glyph(f, cp);
                if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
                    continue;
                float gw = (float)gl_->w * f->scale;
                float gh = (float)gl_->h * f->scale;
                float gx = px0 + (float)col * cw + (float)gl_->bearing_x * f->scale;
                float gy = bar_gl + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
                emit_quad(&vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1,
                          fg, bg, 1.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Which region owns the keyboard, drawn last so nothing covers it. With the file manager off this is
       the side-by-side focus border and nothing else (with one pane a frame around the whole terminal
       would be noise). With it on, exactly one of the three regions gets the frame — the browser, the
       preview, or the terminal pane — because Ctrl+Shift+Up/Down move the keyboard between them and a
       switch the user cannot see is a switch the user will not notice. */
    col2f(border_col, bg);
    bg[3] *= r->opacity;
    if (fm && fm->on && lay->fm.on) {
        int x, y, w, h;
        if (fm->focus == FM_FOCUS_LIST && fm_pane_rect(&lay->fm, 0, &x, &y, &w, &h)) {
            emit_border(&vp, (float)x, (float)y, (float)w, (float)h, 2.0f, bg, du, dv,
                        (float)r->vh);
            nquads += 4;
        } else if (fm->focus == FM_FOCUS_PREVIEW && fm_pane_rect(&lay->fm, 1, &x, &y, &w, &h)) {
            emit_border(&vp, (float)x, (float)y, (float)w, (float)h, 2.0f, bg, du, dv,
                        (float)r->vh);
            nquads += 4;
        }
    }
    if (lay->npane > 1 || (fm && fm->on && lay->fm.on && fm->focus == FM_FOCUS_TERM)) {
        int ap = layout_pane_of_tab(lay, lay->active_tab);
        if (ap >= 0) {
            const PaneGeom *pg = &lay->pane[ap];
            emit_border(&vp, (float)pg->x, (float)pg->y, (float)pg->w, (float)pg->h, 2.0f, bg,
                        du, dv, (float)r->vh);
            nquads += 4;
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
    /* Clear every frame: the padding margin, the gaps between panes and the whole-cell remainders are
       outside every quad, so nothing else writes them, and after a resize the new back buffer has no
       defined contents. Cleared ONCE per frame — clearing again between the batches would erase the
       batch already drawn. */
    set_clear_color(r);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(r->vao);
    setup_draw_state(r, f, selc);

    /* batch A: backgrounds */
    draw_quads(r, vdata, nq_bg);

    /* the pictures, each in its own draw call because each has its own texture */
    int frame = ++r->img_frame;
    int nprev = draw_fm_preview(r, fm, lay, cw, ch);
    int nimg = nprev;
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        nimg += draw_images(r, t, &lay->pane[p], &pd, cw, ch, frame);
    }

    /* One line per CHANGE in how many pictures are on screen — the client-side evidence for any
       "the picture is missing" report, and proof that the renderer reached the image path at all
       (a GLSL or texture failure shows up as the matching warning above it). Not one line a frame.
       The file-manager preview is counted SEPARATELY and named: it has its own texture (Render.fm_tex,
       not the ImgTex cache `r->nimgtex` counts), so folding it into the same number made the line claim
       a texture was held when none of the cached ones were, and made a preview look like an application
       picture. */
    static int last_app = -1, last_prev = -1;
    if (last_app != nimg - nprev || last_prev != nprev) {
        last_app = nimg - nprev;
        last_prev = nprev;
        klog(L_DEBUG, "graphics: %d image(s) drawn, %d texture(s) held%s",
             nimg - nprev, r->nimgtex,
             nprev ? " + the file-manager preview" : "");
    }

    /* batch B: selection bands, glyphs, bar labels, focus border — on top of the pictures, so text
       stays readable over a picture the way the protocol expects. */
    setup_draw_state(r, f, selc);
    draw_quads(r, vdata + (size_t)nq_bg * FLOATS_PER_INST, nquads - nq_bg);

    /* textures of pictures that were not on screen this frame go now: after the last draw that could
       have used them, before the buffer is presented */
    img_tex_sweep(r, frame);

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
        /* while the context is still current — glDeleteTextures is a no-op without one */
        for (int i = 0; i < r->nimgtex; i++)
            if (r->imgtex[i].tex) {
                GLuint t = r->imgtex[i].tex;
                glDeleteTextures(1, &t);
            }
        if (r->fm_tex) {
            GLuint t = r->fm_tex;
            glDeleteTextures(1, &t);
        }
    }
    free(r->imgtex);
    free(r->fm_px);
    r->fm_tex = 0;
    r->fm_tex_uid = 0;
    r->fm_px = NULL;
    r->fm_px_cap = 0;
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
