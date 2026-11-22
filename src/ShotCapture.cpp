#include "ShotCommon.hpp"

using namespace geode::prelude;

namespace tg::shot {

#ifndef GEODE_IS_ANDROID
namespace {
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
}
#endif

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

}
