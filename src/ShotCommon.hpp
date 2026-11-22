#pragma once

// GL helpers shared by the screenshot capture code (moved out of
// Screenshot.cpp unchanged). Render/main thread only.
#include <Geode/Geode.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace tg::shot {

constexpr size_t kMaxImageBytes = 32 * 1024 * 1024;
constexpr int kMaxDimension = 8192;

struct Frame {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

inline bool imageSize(int w, int h, size_t& bytes) {
    if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension) return false;
    auto row = size_t(w) * 4;
    if (size_t(h) > kMaxImageBytes / row) return false;
    bytes = row * size_t(h);
    return bytes <= size_t(std::numeric_limits<int>::max());
}

// Error flags cannot be put back. Drain (and report) stale errors before doing
// our own work, and never encode a buffer following a failed GL operation.
inline bool checkGL(char const* stage) {
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        auto error = glGetError();
        if (error == GL_NO_ERROR) return ok;
        if (ok) geode::log::warn("Screenshot {}: GL error {}", stage, static_cast<unsigned>(error));
        ok = false;
    }
    return false;
}

inline bool hasExtension(char const* extensions, char const* name) {
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
// ShotCaptureAndroid.cpp: renders the running scene into an offscreen target.
bool captureScene(Frame& frame, Caps const& caps);
#endif

// ShotCapture.cpp: captures the finished frame; logs and returns false on failure.
bool captureFrame(Frame& frame);

}
