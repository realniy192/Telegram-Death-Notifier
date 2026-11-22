#include "ShotCommon.hpp"

#ifdef GEODE_IS_ANDROID

#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace geode::prelude;

namespace tg::shot {
namespace {
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
}

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

}

#endif
