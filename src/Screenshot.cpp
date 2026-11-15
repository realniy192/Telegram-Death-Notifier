#include "Screenshot.hpp"
#include "SecureTokenSetting.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr size_t kMaxCaptions = 8;
constexpr size_t kMaxCaptionBytes = 4096;
constexpr size_t kMaxImageBytes = 32 * 1024 * 1024;
constexpr size_t kMaxPNGBytes = 10 * 1024 * 1024;
constexpr size_t kMaxQueuedPNGBytes = 24 * 1024 * 1024;
constexpr int kMaxDimension = 8192;
std::mutex s_queueMutex;
std::atomic_bool s_busy = false;

// Held only through capture/encoding. Upload backpressure is independent, so
// an in-flight HTTP request does not suppress the next attempt's screenshot.
struct BatchLease {
    ~BatchLease() { s_busy.store(false); }
};
struct Frame {
    int width = 0, height = 0;
    ByteVector pixels;
};

bool imageSize(int w, int h, size_t& bytes) {
    if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension) return false;
    auto row = size_t(w) * 4;
    if (size_t(h) > kMaxImageBytes / row) return false;
    bytes = row * size_t(h);
    return bytes <= size_t(std::numeric_limits<int>::max());
}

// Error flags cannot be put back. Drain (and report) stale errors before doing
// our own work, and never encode a buffer following a failed GL operation.
bool checkGL(char const* stage) {
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        auto error = glGetError();
        if (error == GL_NO_ERROR) return ok;
        if (ok) log::warn("Screenshot {}: GL error {}", stage, static_cast<unsigned>(error));
        ok = false;
    }
    return false;
}

bool hasExtension(char const* extensions, char const* name) {
    if (!extensions) return false;
    size_t length = std::strlen(name);
    for (auto p = extensions; (p = std::strstr(p, name)); p += length) {
        if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || p[length] == '\0')) return true;
    }
    return false;
}

// Numeric tokens are intentional: SDK 5.11.2's Android headers are GLES2.
// These use GLES2 entry points, and the additional tokens are used ONLY when
// the current context version/extension advertises the corresponding feature.
constexpr GLenum kPackRowLength = 0x0D02, kPackSkipRows = 0x0D03, kPackSkipPixels = 0x0D04;
constexpr GLenum kPackBuffer = 0x88EB, kPackBufferBinding = 0x88ED;
constexpr GLenum kUnpackRowLength = 0x0CF2, kUnpackSkipRows = 0x0CF3, kUnpackSkipPixels = 0x0CF4;
constexpr GLenum kUnpackBuffer = 0x88EC, kUnpackBufferBinding = 0x88EF;
constexpr GLenum kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kPackReverseANGLE = 0x93A4, kPackInvertMESA = 0x8758;

struct Caps {
    bool es = false, es3 = false, packRows = false, unpackRows = false;
    bool pbo = false, separateFBO = false, packedDepthStencil = false;
    bool vao = false, reverseANGLE = false, invertMESA = false;
    bool init() {
        auto version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
        if (!version) return false;
        es = std::strstr(version, "OpenGL ES") != nullptr;
        auto digits = version;
        while (*digits && (*digits < '0' || *digits > '9')) ++digits;
        int major = 0, minor = 0;
        if (std::sscanf(digits, "%d.%d", &major, &minor) != 2) return false;
        es3 = es && major >= 3;
        // glGetString(GL_EXTENSIONS) is not legal on desktop core profiles.
        auto extensions = (es || major < 3)
            ? reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS)) : nullptr;
        packRows = !es || es3 || hasExtension(extensions, "GL_NV_pack_subimage");
        unpackRows = !es || es3 || hasExtension(extensions, "GL_EXT_unpack_subimage");
        pbo = es3 || (!es && (major > 2 || (major == 2 && minor >= 1)
            || hasExtension(extensions, "GL_ARB_pixel_buffer_object")));
        separateFBO = major >= 3 || (!es && hasExtension(extensions, "GL_ARB_framebuffer_object"));
        packedDepthStencil = es3 || hasExtension(extensions, "GL_OES_packed_depth_stencil");
        vao = hasExtension(extensions, "GL_OES_vertex_array_object");
        reverseANGLE = hasExtension(extensions, "GL_ANGLE_pack_reverse_row_order");
        invertMESA = hasExtension(extensions, "GL_MESA_pack_invert");
        return checkGL("context query");
    }
};

struct PixelStore {
    Caps caps;
    bool unpack;
    GLint packAlignment = 4, unpackAlignment = 4, packBuffer = 0, unpackBuffer = 0;
    GLint packRows = 0, packSkipRows = 0, packSkipPixels = 0;
    GLint unpackRows = 0, unpackSkipRows = 0, unpackSkipPixels = 0;
    GLint reverse = 0, invert = 0;
    PixelStore(Caps const& c, bool saveUnpack) : caps(c), unpack(saveUnpack) {
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
        if (caps.packRows) {
            glGetIntegerv(kPackRowLength, &packRows);
            glGetIntegerv(kPackSkipRows, &packSkipRows);
            glGetIntegerv(kPackSkipPixels, &packSkipPixels);
        }
        if (caps.pbo) glGetIntegerv(kPackBufferBinding, &packBuffer);
        if (caps.reverseANGLE) glGetIntegerv(kPackReverseANGLE, &reverse);
        if (caps.invertMESA) glGetIntegerv(kPackInvertMESA, &invert);
        if (unpack) {
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
            if (caps.unpackRows) {
                glGetIntegerv(kUnpackRowLength, &unpackRows);
                glGetIntegerv(kUnpackSkipRows, &unpackSkipRows);
                glGetIntegerv(kUnpackSkipPixels, &unpackSkipPixels);
            }
            if (caps.pbo) glGetIntegerv(kUnpackBufferBinding, &unpackBuffer);
        }
    }
    void tight() const {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (caps.packRows) {
            glPixelStorei(kPackRowLength, 0);
            glPixelStorei(kPackSkipRows, 0);
            glPixelStorei(kPackSkipPixels, 0);
        }
        if (caps.pbo) glBindBuffer(kPackBuffer, 0);
        if (caps.reverseANGLE) glPixelStorei(kPackReverseANGLE, 0);
        if (caps.invertMESA) glPixelStorei(kPackInvertMESA, 0);
        if (unpack) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (caps.unpackRows) {
                glPixelStorei(kUnpackRowLength, 0);
                glPixelStorei(kUnpackSkipRows, 0);
                glPixelStorei(kUnpackSkipPixels, 0);
            }
            if (caps.pbo) glBindBuffer(kUnpackBuffer, 0);
        }
    }
    ~PixelStore() {
        glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
        if (caps.packRows) {
            glPixelStorei(kPackRowLength, packRows);
            glPixelStorei(kPackSkipRows, packSkipRows);
            glPixelStorei(kPackSkipPixels, packSkipPixels);
        }
        if (caps.pbo) glBindBuffer(kPackBuffer, packBuffer);
        if (caps.reverseANGLE) glPixelStorei(kPackReverseANGLE, reverse);
        if (caps.invertMESA) glPixelStorei(kPackInvertMESA, invert);
        if (unpack) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
            if (caps.unpackRows) {
                glPixelStorei(kUnpackRowLength, unpackRows);
                glPixelStorei(kUnpackSkipRows, unpackSkipRows);
                glPixelStorei(kUnpackSkipPixels, unpackSkipPixels);
            }
            if (caps.pbo) glBindBuffer(kUnpackBuffer, unpackBuffer);
        }
    }
};

struct FramebufferState {
    bool separate;
    GLint framebuffer = 0, readFramebuffer = 0, renderbuffer = 0;
    GLint viewport[4] = {};
    explicit FramebufferState(Caps const& caps) : separate(caps.separateFBO) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (separate) glGetIntegerv(kReadFramebufferBinding, &readFramebuffer);
    }
    ~FramebufferState() {
        if (separate) {
            glBindFramebuffer(kDrawFramebuffer, framebuffer);
            glBindFramebuffer(kReadFramebuffer, readFramebuffer);
        }
        else glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

#ifdef GEODE_IS_ANDROID
// Kazmath has no get-current-mode API. Temporarily replace the *current* stack
// top with a unique marker, inspect the three stacks, then pop it untouched.
// Never invalidate the entire Cocos GL cache: that can free Kazmath stacks.
struct MatrixState {
    kmGLEnum oldMode = KM_GL_MODELVIEW;
    MatrixState() {
        constexpr kmGLEnum modes[] = {KM_GL_MODELVIEW, KM_GL_PROJECTION, KM_GL_TEXTURE};
        std::array<kmMat4, 3> before;
        for (size_t i = 0; i < 3; ++i) kmGLGetMatrix(modes[i], &before[i]);
        kmMat4 marker;
        kmMat4Identity(&marker);
        marker.mat[0] = 17.f;
        while (marker.mat[0] == before[0].mat[0] || marker.mat[0] == before[1].mat[0]
            || marker.mat[0] == before[2].mat[0]) marker.mat[0] += 1.f;
        kmGLPushMatrix();
        kmGLLoadMatrix(&marker);
        for (auto mode : modes) {
            kmMat4 current;
            kmGLGetMatrix(mode, &current);
            if (std::memcmp(&current, &marker, sizeof(marker)) == 0) oldMode = mode;
        }
        kmGLPopMatrix();
        for (auto mode : modes) { kmGLMatrixMode(mode); kmGLPushMatrix(); }
        kmGLMatrixMode(KM_GL_MODELVIEW);
    }
    ~MatrixState() {
        for (auto mode : {KM_GL_TEXTURE, KM_GL_PROJECTION, KM_GL_MODELVIEW}) {
            kmGLMatrixMode(mode);
            kmGLPopMatrix();
        }
        kmGLMatrixMode(oldMode);
        ccSetProjectionMatrixDirty();
    }
};

struct StencilFace {
    GLint func = 0, ref = 0, valueMask = 0, writeMask = 0, fail = 0, depthFail = 0, pass = 0;
    void save(bool back) {
        glGetIntegerv(back ? GL_STENCIL_BACK_FUNC : GL_STENCIL_FUNC, &func);
        glGetIntegerv(back ? GL_STENCIL_BACK_REF : GL_STENCIL_REF, &ref);
        glGetIntegerv(back ? GL_STENCIL_BACK_VALUE_MASK : GL_STENCIL_VALUE_MASK, &valueMask);
        glGetIntegerv(back ? GL_STENCIL_BACK_WRITEMASK : GL_STENCIL_WRITEMASK, &writeMask);
        glGetIntegerv(back ? GL_STENCIL_BACK_FAIL : GL_STENCIL_FAIL, &fail);
        glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_FAIL : GL_STENCIL_PASS_DEPTH_FAIL, &depthFail);
        glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_PASS : GL_STENCIL_PASS_DEPTH_PASS, &pass);
    }
    void restore(GLenum face) const {
        glStencilFuncSeparate(face, func, ref, static_cast<GLuint>(valueMask));
        glStencilMaskSeparate(face, static_cast<GLuint>(writeMask));
        glStencilOpSeparate(face, fail, depthFail, pass);
    }
};

// A scene visit can change considerably more state than glReadPixels. Keep the
// Cocos caches consistent as well as restoring the actual driver state.
struct SceneGLState {
    static constexpr std::array<GLenum, 9> enables = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST,
        GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_POLYGON_OFFSET_FILL, GL_DITHER,
        GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
    std::array<GLboolean, enables.size()> enabled{};
    GLint program = 0, activeTexture = GL_TEXTURE0, arrayBuffer = 0, elementBuffer = 0, vao = 0;
    GLint scissor[4] = {}, srcRGB = 0, dstRGB = 0, srcAlpha = 0, dstAlpha = 0;
    GLint equationRGB = 0, equationAlpha = 0, depthFunc = 0, stencilClear = 0;
    GLint cullMode = 0, frontFace = 0;
    GLfloat clearColor[4] = {}, blendColor[4] = {}, depthRange[2] = {}, depthClear = 1;
    GLfloat lineWidth = 1, polygonFactor = 0, polygonUnits = 0, coverage = 1;
    GLboolean colorMask[4] = {}, depthMask = GL_TRUE, coverageInvert = GL_FALSE;
    GLboolean rasterDiscard = GL_FALSE;
    bool hasVAO = false, es3 = false, saved = false;
    StencilFace front, back;
    std::vector<GLint> textures;
    struct Attrib {
        GLint enabled = 0, size = 4, type = GL_FLOAT, normalized = 0, stride = 0, buffer = 0;
        void* pointer = nullptr;
        GLfloat value[4] = {};
    };
    std::vector<Attrib> attribs;
    bool save(Caps const& caps) {
        es3 = caps.es3;
        hasVAO = caps.vao && glBindVertexArray;
        GLint textureCount = 0, attribCount = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureCount);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attribCount);
        if (textureCount < 1 || textureCount > 192 || attribCount < 3 || attribCount > 32) return false;
        textures.resize(textureCount);
        attribs.resize(attribCount);
        // GLES3 integer pointers require GLES3-only entry points to restore.
        // This GLES2 module drops that unusual case instead of changing them.
        if (es3) {
            for (int i = 0; i < attribCount; ++i) {
                GLint integer = 0;
                glGetVertexAttribiv(i, 0x88FD /* GL_VERTEX_ATTRIB_ARRAY_INTEGER */, &integer);
                if (integer) return false;
            }
        }
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int i = 0; i < textureCount; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
        }
        glActiveTexture(activeTexture);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
        if (hasVAO) glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
        for (int i = 0; i < attribCount; ++i) {
            auto& a = attribs[i];
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
            glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
            glGetVertexAttribfv(i, GL_CURRENT_VERTEX_ATTRIB, a.value);
        }
        for (size_t i = 0; i < enables.size(); ++i) enabled[i] = glIsEnabled(enables[i]);
        if (es3) rasterDiscard = glIsEnabled(0x8C89 /* GL_RASTERIZER_DISCARD */);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
        glGetFloatv(GL_BLEND_COLOR, blendColor); glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc); glGetFloatv(GL_DEPTH_RANGE, depthRange);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depthClear); glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &stencilClear);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode); glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetFloatv(GL_LINE_WIDTH, &lineWidth); glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygonFactor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygonUnits);
        glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &coverage); glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &coverageInvert);
        front.save(false); back.save(true);
        saved = checkGL("scene state query");
        return saved;
    }
    ~SceneGLState() {
        if (!saved) return;
        ccGLUseProgram(program); glUseProgram(program);
        ccGLBlendFunc(srcRGB, dstRGB);
        glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
        glBlendEquationSeparate(equationRGB, equationAlpha);
        glBlendColor(blendColor[0], blendColor[1], blendColor[2], blendColor[3]);
        // The Cocos 2.x texture cache has 16 entries; higher units are GL-only.
        for (size_t i = 0; i < textures.size(); ++i) {
            if (i < 16) ccGLBindTexture2DN(static_cast<GLuint>(i), textures[i]);
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, textures[i]);
        }
        glActiveTexture(activeTexture);
        if (hasVAO) { ccGLBindVAO(vao); glBindVertexArray(vao); }
        unsigned flags = 0;
        for (unsigned i = 0; i < 3; ++i) if (attribs[i].enabled) flags |= 1u << i;
        ccGLEnableVertexAttribs(flags);
        // ccGLEnableVertexAttribs can bind VAO zero internally.
        if (hasVAO) { ccGLBindVAO(vao); glBindVertexArray(vao); }
        for (GLuint i = 0; i < attribs.size(); ++i) {
            auto const& a = attribs[i];
            glBindBuffer(GL_ARRAY_BUFFER, a.buffer);
            glVertexAttribPointer(i, a.size, a.type, static_cast<GLboolean>(a.normalized), a.stride, a.pointer);
            if (a.enabled) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
            glVertexAttrib4fv(i, a.value);
        }
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDepthMask(depthMask); glDepthFunc(depthFunc); glDepthRangef(depthRange[0], depthRange[1]);
        glClearDepthf(depthClear); glClearStencil(stencilClear);
        front.restore(GL_FRONT); back.restore(GL_BACK);
        glCullFace(cullMode); glFrontFace(frontFace); glLineWidth(lineWidth);
        glPolygonOffset(polygonFactor, polygonUnits); glSampleCoverage(coverage, coverageInvert);
        for (size_t i = 0; i < enables.size(); ++i) {
            if (enabled[i]) glEnable(enables[i]); else glDisable(enables[i]);
        }
        if (es3) {
            if (rasterDiscard) glEnable(0x8C89); else glDisable(0x8C89);
        }
    }
};

struct RenderTarget {
    CCRenderTexture* texture = nullptr;
    GLuint depth = 0, stencil = 0;
    bool begun = false;
    ~RenderTarget() {
        if (begun) texture->end();
        if (depth) glDeleteRenderbuffers(1, &depth);
        if (stencil) glDeleteRenderbuffers(1, &stencil);
        if (texture) texture->release();
    }
};

bool captureScene(Frame& frame, Caps const& caps) {
    auto director = CCDirector::get();
    auto scene = director->getRunningScene();
    auto view = director->getOpenGLView();
    if (!scene || !view) return false;
    auto logical = director->getWinSize();
    auto physical = view->getFrameSize();
    float scale = director->getContentScaleFactor();
    if (!std::isfinite(logical.width) || !std::isfinite(logical.height)
        || !std::isfinite(scale) || logical.width <= 0 || logical.height <= 0 || scale <= 0
        || logical.width > kMaxDimension || logical.height > kMaxDimension
        || !std::isfinite(physical.width) || !std::isfinite(physical.height)
        || physical.width < 1 || physical.height < 1
        || physical.width > kMaxDimension || physical.height > kMaxDimension
        || std::abs(physical.width - std::round(physical.width)) > .01f
        || std::abs(physical.height - std::round(physical.height)) > .01f) return false;
    // The Android view reports the drawable raster. Do not substitute design
    // resolution, infer a retina factor, or guess a width/height rotation.
    int physicalW = static_cast<int>(std::round(physical.width));
    int physicalH = static_cast<int>(std::round(physical.height));
    if (director->getProjection() == kCCDirectorProjectionCustom) {
        log::warn("Screenshot dropped: custom director projection is unsupported");
        return false;
    }
    GLint maxTexture = 0, maxRenderbuffer = 0, maxViewport[2] = {};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
    // RenderTexture takes integer POINTS and applies the content scale. Round
    // points up to contain the entire physical raster; discard only this
    // allocation rounding/padding, never drawable pixels, during readback.
    double requiredPointsW = std::ceil(double(physicalW) / scale);
    double requiredPointsH = std::ceil(double(physicalH) / scale);
    if (requiredPointsW < 1 || requiredPointsH < 1
        || requiredPointsW > std::numeric_limits<int>::max()
        || requiredPointsH > std::numeric_limits<int>::max()) return false;
    int pointsW = static_cast<int>(requiredPointsW);
    int pointsH = static_cast<int>(requiredPointsH);
    // Budget conservatively for POT backing before asking Cocos to allocate.
    double expectedW = std::ceil(double(pointsW) * scale), expectedH = std::ceil(double(pointsH) * scale);
    if (expectedW < 1 || expectedH < 1 || expectedW > kMaxDimension || expectedH > kMaxDimension) return false;
    int potW = 1, potH = 1;
    while (potW < expectedW) potW *= 2;
    while (potH < expectedH) potH *= 2;
    size_t backingBytes = 0;
    if (!imageSize(potW, potH, backingBytes) || potW > maxTexture || potH > maxTexture
        || potW > maxRenderbuffer || potH > maxRenderbuffer) return false;

    FramebufferState framebuffer(caps);
    PixelStore pixelStore(caps, true);
    SceneGLState glState;
    if (!glState.save(caps) || !checkGL("state save")) return false;
    MatrixState matrices;
    pixelStore.tight();
    if (!checkGL("pixel store setup")) return false;
    RenderTarget target;
    // Own it directly: autorelease would keep large GL textures alive until a
    // later pool drain. Destruction remains on the render/main thread.
    target.texture = new CCRenderTexture();
    if (!target.texture->initWithWidthAndHeight(pointsW, pointsH, kCCTexture2DPixelFormat_RGBA8888)
        || !checkGL("render texture creation")) return false;
    auto texture = target.texture->m_pTexture;
    if (!texture || !target.texture->m_uFBO) return false;
    auto content = texture->getContentSizeInPixels();
    auto bw = texture->getPixelsWide(), bh = texture->getPixelsHigh();
    // Content is image pixels; PixelsWide/High can include POT padding. Never
    // size a read from logical points or include the padding in the PNG.
    if (!std::isfinite(content.width) || !std::isfinite(content.height)
        || content.width < 1 || content.height < 1 || content.width > kMaxDimension || content.height > kMaxDimension
        || std::abs(content.width - std::round(content.width)) > .01f
        || std::abs(content.height - std::round(content.height)) > .01f
        || bw > unsigned(maxTexture) || bh > unsigned(maxTexture)
        || bw > unsigned(maxRenderbuffer) || bh > unsigned(maxRenderbuffer)
        || bw > unsigned(kMaxDimension) || bh > unsigned(kMaxDimension)) return false;
    frame.width = physicalW;
    frame.height = physicalH;
    size_t bytes = 0;
    if (frame.width > content.width || frame.height > content.height
        || unsigned(frame.width) > bw || unsigned(frame.height) > bh
        || frame.width > maxViewport[0] || frame.height > maxViewport[1]
        || !imageSize(frame.width, frame.height, bytes)
        || !imageSize(static_cast<int>(bw), static_cast<int>(bh), backingBytes)) return false;
    frame.pixels.resize(bytes);
    target.texture->begin();
    target.begun = true;
    glBindFramebuffer(GL_FRAMEBUFFER, target.texture->m_uFBO);
    // Clipping nodes need stencil storage; color-only RenderTexture does not
    // provide it. Packed depth/stencil is optional in ES2, so use separate
    // core ES2 renderbuffers when that extension is absent (check completeness).
    glGenRenderbuffers(1, &target.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, caps.packedDepthStencil ? GLenum(0x88F0) : GL_DEPTH_COMPONENT16, bw, bh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depth);
    if (caps.packedDepthStencil) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
    }
    else {
        glGenRenderbuffers(1, &target.stencil);
        glBindRenderbuffer(GL_RENDERBUFFER, target.stencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, bw, bh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.stencil);
    }
    if (!checkGL("offscreen attachments") || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::warn("Screenshot dropped: incomplete offscreen framebuffer");
        return false;
    }
    glViewport(0, 0, frame.width, frame.height);
    // begin() can multiply an inherited projection. Replace BOTH ambient
    // stacks, not scene/node transforms: in-game camera rotation must survive.
    kmMat4 p;
    kmMat4OrthographicProjection(&p, 0, logical.width, 0, logical.height, -1024, 1024);
    kmGLMatrixMode(KM_GL_PROJECTION); kmGLLoadIdentity(); kmGLLoadMatrix(&p);
    kmGLMatrixMode(KM_GL_MODELVIEW); kmGLLoadIdentity();
    ccSetProjectionMatrixDirty();
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (caps.es3) glDisable(0x8C89 /* GL_RASTERIZER_DISCARD */);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE); glStencilMask(~GLuint(0));
    glClearColor(0, 0, 0, 1); glClearDepthf(1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    // Match the director's normal 2D scene baseline; node-specific effects can
    // turn depth/stencil on during their visit as usual.
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glBlendEquation(GL_FUNC_ADD);
    if (!checkGL("scene setup")) return false;
    scene->visit();
    GLint drawnFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawnFBO);
    if (drawnFBO != static_cast<GLint>(target.texture->m_uFBO) || !checkGL("offscreen scene visit")) return false;
    // Nested scene render textures may have changed pixel-store or read FBO.
    glBindFramebuffer(GL_FRAMEBUFFER, target.texture->m_uFBO);
    pixelStore.tight();
    if (!checkGL("readback setup")) return false;
    glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    return checkGL("offscreen readback");
}
#else
bool captureFramebuffer(Frame& frame, Caps const& caps) {
    FramebufferState framebuffer(caps);
    PixelStore pixelStore(caps, false);
    if (!checkGL("framebuffer state query")) return false;
    auto const* vp = framebuffer.viewport;
    size_t bytes = 0;
    if (vp[0] < 0 || vp[1] < 0 || !imageSize(vp[2], vp[3], bytes)
        || vp[0] > std::numeric_limits<int>::max() - vp[2]
        || vp[1] > std::numeric_limits<int>::max() - vp[3]) return false;
    // Read the finished DRAW framebuffer, not an unrelated read target left by
    // another mod. Never guess a viewport from logical design resolution.
    if (caps.separateFBO) glBindFramebuffer(kReadFramebuffer, framebuffer.framebuffer);
    auto readTarget = caps.separateFBO ? kReadFramebuffer : GLenum(GL_FRAMEBUFFER);
    if (glCheckFramebufferStatus(readTarget) != GL_FRAMEBUFFER_COMPLETE) return false;
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (framebuffer.framebuffer && samples > 0) {
        log::warn("Screenshot dropped: multisampled offscreen framebuffer needs a resolve");
        return false;
    }
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    struct ReadBufferState {
        GLint value = 0;
        ReadBufferState() { glGetIntegerv(GL_READ_BUFFER, &value); }
        ~ReadBufferState() { glReadBuffer(value); }
    } readBuffer;
    if (!framebuffer.framebuffer) {
        GLboolean doubleBuffered = GL_FALSE;
        glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
        glReadBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
    }
#endif
    pixelStore.tight();
    if (!checkGL("framebuffer readback setup")) return false;
    frame.width = vp[2]; frame.height = vp[3];
    frame.pixels.resize(bytes);
    glReadPixels(vp[0], vp[1], frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    return checkGL("framebuffer readback");
}
#endif

// ======================= upload queue =======================
constexpr size_t kMaxUploadJobs = 12;
constexpr size_t kPhotoCaptionLimit = 1024; // Telegram sendPhoto caption limit

std::atomic<size_t> s_queuedPNGBytes = 0;

// Counts its own bytes for as long as any job still references it.
struct QueuedPNG {
    ByteVector data;
    explicit QueuedPNG(ByteVector bytes) : data(std::move(bytes)) { s_queuedPNGBytes += data.size(); }
    ~QueuedPNG() { s_queuedPNGBytes -= data.size(); }
};

struct UploadJob {
    std::shared_ptr<QueuedPNG const> png; // null: text message
    std::string text;
};

std::mutex s_uploadMutex;
std::deque<UploadJob> s_jobs;
bool s_uploadActive = false;

size_t freePNGBudget() {
    return kMaxQueuedPNGBytes - std::min(s_queuedPNGBytes.load(), kMaxQueuedPNGBytes);
}

void sendToTelegram();

void uploadFinished() {
    {
        std::lock_guard lock(s_uploadMutex);
        s_uploadActive = false;
    }
    sendToTelegram();
}

// One HTTP request at a time, always on the main thread.
void sendToTelegram() {
    UploadJob job;
    {
        std::lock_guard lock(s_uploadMutex);
        if (s_uploadActive || s_jobs.empty()) return;
        s_uploadActive = true;
        job = std::move(s_jobs.front());
        s_jobs.pop_front();
    }
    try {
        auto mod = Mod::get();
        auto token = std::string(utils::string::trim(tg::botToken()));
        auto chat = std::string(utils::string::trim(mod->getSettingValue<std::string>("chat-id")));
        if (token.starts_with("bot")) token = token.substr(3);
        if (token.empty() || chat.empty()) {
            uploadFinished();
            return;
        }
        auto timeout = std::clamp<int64_t>(mod->getSettingValue<int64_t>("upload-timeout"), 5, 120);
        web::MultipartForm form;
        form.param("chat_id", chat);
        char const* method = "sendMessage";
        if (job.png) {
            method = "sendPhoto";
            if (!job.text.empty()) form.param("caption", job.text);
            form.file("photo", job.png->data, "shot.png", "image/png");
        }
        else form.param("text", job.text);
        auto req = web::WebRequest();
        req.bodyMultipart(form);
        req.timeout(std::chrono::seconds(timeout));
        async::spawn(req.post(fmt::format("https://api.telegram.org/bot{}/{}", token, method)),
            [png = job.png](web::WebResponse res) {
                try {
                    if (!res.ok()) {
                        // Do not log a URL/body that might contain a bot token.
                        log::warn("Telegram upload failed (HTTP {})", res.code());
                    }
                    else {
                        auto json = res.json();
                        if (!json || !json.unwrap()["ok"].asBool().unwrapOr(false))
                            log::warn("Telegram rejected the message or returned an invalid response");
                    }
                }
                catch (std::exception const&) {
                    log::warn("Could not inspect Telegram response");
                }
                uploadFinished();
            });
    }
    catch (std::exception const&) {
        log::warn("Telegram upload could not be started; job dropped");
        uploadFinished();
    }
}

bool enqueueJobs(std::vector<UploadJob> jobs) {
    {
        std::lock_guard lock(s_uploadMutex);
        if (s_jobs.size() + jobs.size() > kMaxUploadJobs) {
            log::warn("Telegram upload queue is full; dropped");
            return false;
        }
        for (auto& job : jobs) s_jobs.push_back(std::move(job));
    }
    geode::queueInMainThread([] { sendToTelegram(); });
    return true;
}

// ---------- deferred captions ----------
// A deferred shot is captured first and gets its caption later (rating panel).
constexpr size_t kMaxParked = 4;
std::mutex s_deferMutex;
std::set<uint64_t> s_waiting;                                     // still expect a caption
std::map<uint64_t, std::shared_ptr<QueuedPNG const>> s_parked;    // encoded, caption pending
std::map<uint64_t, tg::ShotText> s_resolved;                      // caption came before the PNG

void appendJobs(std::vector<UploadJob>& jobs, std::shared_ptr<QueuedPNG const> const& png, tg::ShotText text) {
    if (text.caption.size() <= kPhotoCaptionLimit) jobs.push_back({png, std::move(text.caption)});
    else {
        jobs.push_back({png, ""});
        jobs.push_back({nullptr, std::move(text.caption)});
    }
    // Follows its own photo, so the order in the channel is always right.
    if (!text.followUp.empty()) jobs.push_back({nullptr, std::move(text.followUp)});
}

void forgetTicket(uint64_t ticket) {
    std::lock_guard lock(s_deferMutex);
    s_waiting.erase(ticket);
    s_parked.erase(ticket);
    s_resolved.erase(ticket);
}

// One PNG, one job per caption. Captions over the photo limit are sent as a
// clean photo followed by a text message, so long stats are never rejected.
void enqueuePhoto(ByteVector bytes, std::vector<tg::ShotText> captions, bool noCaption) {
    if (bytes.size() > freePNGBudget()) {
        log::warn("Telegram upload queue is full; encoded frame dropped");
        for (auto const& text : captions) if (text.ticket) forgetTicket(text.ticket);
        return;
    }
    auto png = std::make_shared<QueuedPNG const>(std::move(bytes));
    std::vector<UploadJob> jobs;
    if (noCaption || captions.empty()) {
        jobs.push_back({png, ""});
        for (auto const& text : captions) if (text.ticket) forgetTicket(text.ticket);
    }
    else for (auto& text : captions) {
        if (text.ticket) {
            std::lock_guard lock(s_deferMutex);
            if (auto it = s_resolved.find(text.ticket); it != s_resolved.end()) {
                auto resolved = std::move(it->second);
                s_resolved.erase(it);
                s_waiting.erase(text.ticket);
                appendJobs(jobs, png, std::move(resolved));
            }
            else if (s_waiting.contains(text.ticket) && s_parked.size() < kMaxParked) {
                s_parked[text.ticket] = png;
            }
            else s_waiting.erase(text.ticket);
            continue;
        }
        appendJobs(jobs, png, std::move(text));
    }
    if (!jobs.empty()) enqueueJobs(std::move(jobs));
}

struct TemporaryPNG {
    std::filesystem::path path;
    ~TemporaryPNG() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) log::warn("Could not remove temporary screenshot PNG");
    }
};

// Read on the main thread only; the encoder thread never touches settings.
struct EncodeOptions {
    float skew = 0.f;
    bool noCaption = false;
};

EncodeOptions currentEncodeOptions() {
    auto mod = Mod::get();
    EncodeOptions options;
    if (mod->getSettingValue<bool>("skewed-screenshot")) {
        options.skew = std::clamp((int)mod->getSettingValue<int64_t>("skew-strength"), 10, 400) / 100.f;
    }
    options.noCaption = mod->getSettingValue<bool>("no-caption");
    return options;
}

// Joke mode: recreates the old row-pitch bug. Each row is rotated left a bit
// more than the row above it, so the picture shears diagonally and wraps
// around the edge. Whole pixels move, so colors stay intact.
void applySkew(Frame& frame, float pixelsPerRow) {
    size_t row = size_t(frame.width) * 4;
    for (int y = 0; y < frame.height; ++y) {
        auto shift = size_t(std::lround(y * pixelsPerRow)) % size_t(frame.width);
        if (!shift) continue;
        auto line = frame.pixels.begin() + size_t(y) * row;
        std::rotate(line, line + shift * 4, line + row);
    }
}

void encodeAndSend(Frame frame, std::vector<tg::ShotText> captions, std::shared_ptr<BatchLease> lease,
                   EncodeOptions options) {
    static uint64_t counter = 0; // render thread only
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = Mod::get()->getSaveDir() / fmt::format("shot-{}-{}.png", stamp, counter++);
    // CCObject construction/destruction touch Cocos bookkeeping. Only image
    // pixel initialization and file encoding run on the worker, never GL or a
    // scene/node. Use no autorelease objects on that thread.
    auto image = std::shared_ptr<CCImage>(new CCImage(), [](CCImage* p) {
        geode::queueInMainThread([p] { p->release(); });
    });
    std::thread([frame = std::move(frame), path = std::move(path), image = std::move(image),
        captions = std::move(captions), lease = std::move(lease), options]() mutable {
        try {
            TemporaryPNG temporary{path};
            size_t row = size_t(frame.width) * 4;
            for (int y = 0; y < frame.height / 2; ++y) {
                auto first = frame.pixels.begin() + size_t(y) * row;
                auto opposite = frame.pixels.begin() + size_t(frame.height - 1 - y) * row;
                std::swap_ranges(first, first + row, opposite);
            }
            if (options.skew > 0.f) applySkew(frame, options.skew);
            bool ok = image->initWithImageData(frame.pixels.data(), static_cast<int>(frame.pixels.size()),
                CCImage::kFmtRawData, frame.width, frame.height, 8);
            // Drop the readback copy before PNG encoding allocates its buffers.
            ByteVector().swap(frame.pixels);
            if (!ok || !image->saveToFile(path.string().c_str(), true)) {
                log::warn("Screenshot PNG encoding failed");
                return;
            }
            std::error_code ec;
            auto size = std::filesystem::file_size(path, ec);
            if (ec || !size || size > kMaxPNGBytes) {
                log::warn("Screenshot PNG missing or exceeds the 10 MiB upload limit");
                return;
            }
            auto data = file::readBinary(path);
            if (!data) { log::warn("Could not read encoded screenshot PNG"); return; }
            // Reserve queue memory before releasing the encoding slot.
            enqueuePhoto(std::move(data).unwrap(), std::move(captions), options.noCaption);
            image.reset();
            lease.reset();
        }
        catch (std::exception const&) {
            log::warn("Screenshot encoding failed; frame dropped");
        }
    }).detach();
}

// ======================= pending screenshots =======================
constexpr size_t kMaxFallbackFrames = 2;

struct PendingShot {
    uint64_t id = 0;
    tg::ShotText text;
    std::chrono::steady_clock::time_point due;
    bool wantFallback = false; // delayed: also keep the frame of the moment it was queued
    bool flushed = false;      // level restarted before the delay passed
    std::optional<Frame> fallback;
    std::function<void()> onCaptured;
};

std::vector<PendingShot> s_pending; // guarded by s_queueMutex
uint64_t s_nextShotId = 0;

bool captureFrame(Frame& frame) {
    checkGL("pre-existing state");
    Caps caps;
    bool ok = caps.init();
#ifdef GEODE_IS_ANDROID
    if (ok) ok = captureScene(frame, caps);
#else
    if (ok) ok = captureFramebuffer(frame, caps);
#endif
    // Guards have now restored the context, even when capture failed.
    if (!checkGL("state restoration")) ok = false;
    if (!ok) log::warn("Screenshot capture failed or exceeded limits; frame dropped");
    return ok;
}

void encode(Frame frame, std::vector<tg::ShotText> captions) {
    s_busy.store(true);
    std::shared_ptr<BatchLease> lease;
    try { lease = std::make_shared<BatchLease>(); }
    catch (...) { s_busy.store(false); throw; }
    encodeAndSend(std::move(frame), std::move(captions), std::move(lease), currentEncodeOptions());
}

void storeFallbacks(std::vector<uint64_t> const& ids, Frame const& frame) {
    std::lock_guard lock(s_queueMutex);
    size_t stored = std::count_if(s_pending.begin(), s_pending.end(),
        [](PendingShot const& shot) { return shot.fallback.has_value(); });
    for (auto& shot : s_pending) {
        if (stored >= kMaxFallbackFrames) break;
        if (shot.fallback || std::find(ids.begin(), ids.end(), shot.id) == ids.end()) continue;
        shot.fallback = frame;
        ++stored;
    }
}

void runCallbacks(std::vector<std::function<void()>>& callbacks) {
    for (auto& callback : callbacks) {
        if (callback) geode::queueInMainThread(std::move(callback));
    }
}

void capturePending() {
    // The previous frame is still encoding: keep the queue and retry next frame.
    if (s_busy.load()) return;
    auto now = std::chrono::steady_clock::now();
    std::optional<Frame> restartFrame;
    std::vector<tg::ShotText> captions;
    std::vector<uint64_t> fallbackIds;
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(s_queueMutex);
        if (s_pending.empty()) return;
        auto flushed = std::find_if(s_pending.begin(), s_pending.end(),
            [](PendingShot const& shot) { return shot.flushed && shot.fallback; });
        if (flushed != s_pending.end()) {
            restartFrame = std::move(flushed->fallback);
            captions.push_back(std::move(flushed->text));
            callbacks.push_back(std::move(flushed->onCaptured));
            s_pending.erase(flushed);
        }
        else {
            for (auto it = s_pending.begin(); it != s_pending.end();) {
                if (it->due <= now) {
                    captions.push_back(std::move(it->text));
                    callbacks.push_back(std::move(it->onCaptured));
                    it = s_pending.erase(it);
                    continue;
                }
                if (it->wantFallback && !it->fallback) {
                    fallbackIds.push_back(it->id);
                    it->wantFallback = false; // one attempt per shot
                }
                ++it;
            }
        }
    }
    std::vector<uint64_t> tickets;
    for (auto const& text : captions) if (text.ticket) tickets.push_back(text.ticket);
    bool captured = false;
    try {
        if (restartFrame) {
            encode(std::move(*restartFrame), std::move(captions));
            captured = true;
        }
        else if (!captions.empty() || !fallbackIds.empty()) {
            Frame frame;
            if (captureFrame(frame)) {
                captured = true;
                if (!fallbackIds.empty()) storeFallbacks(fallbackIds, frame);
                if (!captions.empty()) encode(std::move(frame), std::move(captions));
            }
        }
    }
    catch (std::exception const&) {
        captured = false;
        log::warn("Screenshot capture could not be allocated or scheduled; frame dropped");
    }
    // A failed deferred shot will never get a photo: drop its caption slot.
    if (!captured) for (auto ticket : tickets) forgetTicket(ticket);
    runCallbacks(callbacks);
}
} // namespace

uint64_t tg::queueScreenshot(ShotRequest request) {
    if (request.caption.size() > kMaxCaptionBytes || request.followUp.size() > kMaxCaptionBytes) {
        log::warn("Screenshot caption exceeds queue size limit; dropped");
        return 0;
    }
    int delay = std::clamp(request.delayMs, 0, 5000);
    std::lock_guard lock(s_queueMutex);
    if (s_pending.size() >= kMaxCaptions) {
        log::warn("Screenshot queue is full; dropped");
        return 0;
    }
    try {
        PendingShot shot;
        shot.id = ++s_nextShotId;
        shot.text = {std::move(request.caption), std::move(request.followUp), request.deferred ? shot.id : 0};
        shot.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
        shot.wantFallback = delay > 0;
        shot.onCaptured = std::move(request.onCaptured);
        auto id = shot.id;
        if (request.deferred) {
            std::lock_guard defer(s_deferMutex);
            s_waiting.insert(id);
        }
        s_pending.push_back(std::move(shot));
        return id;
    }
    catch (std::exception const&) {
        log::warn("Screenshot could not be queued; dropped");
        return 0;
    }
}

void tg::resolveDeferredScreenshot(uint64_t ticket, ShotText text) {
    if (!ticket) return;
    text.ticket = 0;
    std::vector<UploadJob> jobs;
    {
        std::lock_guard lock(s_deferMutex);
        if (!s_waiting.contains(ticket)) return;
        auto parked = s_parked.find(ticket);
        if (parked == s_parked.end()) {
            s_resolved[ticket] = std::move(text);
            return;
        }
        appendJobs(jobs, parked->second, std::move(text));
        s_parked.erase(parked);
        s_waiting.erase(ticket);
    }
    enqueueJobs(std::move(jobs));
}

void tg::flushDelayedScreenshots() {
    std::lock_guard lock(s_queueMutex);
    auto now = std::chrono::steady_clock::now();
    for (auto& shot : s_pending) {
        if (shot.due <= now) continue;
        if (shot.fallback) shot.flushed = true;
        // Death and restart in the same frame: no earlier frame exists, so
        // capture as soon as possible instead of losing the message.
        else shot.due = now;
    }
}

void tg::sendTelegramText(std::string text) {
    if (text.empty()) return;
    if (text.size() > kMaxCaptionBytes) text.resize(kMaxCaptionBytes);
    std::vector<UploadJob> jobs;
    jobs.push_back({nullptr, std::move(text)});
    enqueueJobs(std::move(jobs));
}

#ifdef GEODE_IS_ANDROID
// Android's actual EGL swap happens in Java after native drawScene returns.
class $modify(TGScreenshotDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        capturePending();
    }
};
#else
class $modify(TGScreenshotView, CCEGLView) {
    void swapBuffers() {
        capturePending();
        CCEGLView::swapBuffers();
    }
};
#endif
