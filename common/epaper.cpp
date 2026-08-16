#include "epaper.h"

#include <QGuiApplication>
#include <QLoggingCategory>

#include <dlfcn.h>

Q_LOGGING_CATEGORY(lcEpaper, "rmchat.epaper")

namespace {
/* Mangled names as exported by libqsgepaper.so on OS 5.8.202:
 *   _ZN13EPFramebuffer8instanceEv
 *       -> EPFramebuffer *EPFramebuffer::instance()
 *   _ZN13EPFramebuffer12ghostControlENS_16GhostControlModeE
 *       -> void EPFramebuffer::ghostControl(GhostControlMode)   [non-static]
 * The disassembly confirms r0 is `this` (it locks a mutex at this+8) and r1
 * is the mode, so the call is ghostControl(instance(), mode).
 */
const char *kInstanceSym = "_ZN13EPFramebuffer8instanceEv";
const char *kGhostSym = "_ZN13EPFramebuffer12ghostControlENS_16GhostControlModeE";
} // namespace

Epaper::Epaper(QObject *parent)
    : QObject(parent)
{
    // Deliberately NOT resolving here: libqsgepaper.so is loaded lazily by Qt
    // when the first QQuickWindow is created, which happens after main() has
    // constructed this object. Resolving now always fails.
}

void Epaper::resolve()
{
    if (m_resolved)
        return;
    m_resolved = true;

    // The plugin is already in-process under `-platform epaper`; RTLD_DEFAULT
    // searches the loaded images. Fall back to loading it explicitly.
    m_instance = reinterpret_cast<void *(*)()>(dlsym(RTLD_DEFAULT, kInstanceSym));
    m_ghostControl = reinterpret_cast<void (*)(void *, int)>(
        dlsym(RTLD_DEFAULT, kGhostSym));

    if (!m_instance || !m_ghostControl) {
        // Already-loaded copy first (RTLD_NOLOAD), then load it outright.
        for (int flags : {RTLD_NOW | RTLD_NOLOAD, RTLD_NOW}) {
            void *lib = dlopen("/usr/lib/plugins/scenegraph/libqsgepaper.so", flags);
            if (!lib)
                continue;
            m_instance = reinterpret_cast<void *(*)()>(dlsym(lib, kInstanceSym));
            m_ghostControl = reinterpret_cast<void (*)(void *, int)>(
                dlsym(lib, kGhostSym));
            if (m_instance && m_ghostControl)
                break;
        }
    }

    qCInfo(lcEpaper) << "full-refresh support:"
                     << (available() ? "available" : "not available (no-op)");
}

void Epaper::blinkLater()
{
    resolve();
    if (!available())
        return;
    if (void *fb = m_instance()) {
        qCInfo(lcEpaper) << "requesting full refresh on next update (BlinkLater)";
        m_ghostControl(fb, BlinkLater);
    }
}

void Epaper::blink()
{
    resolve();
    if (!available()) {
        if (!m_warned) {
            m_warned = true;
            qCInfo(lcEpaper) << "blink() requested but EPFramebuffer is not "
                                "reachable; skipping full refresh";
        }
        return;
    }

    void *fb = m_instance();
    if (!fb) {
        qCWarning(lcEpaper) << "EPFramebuffer::instance() returned null";
        return;
    }
    qCInfo(lcEpaper) << "forcing full-screen refresh (BlinkNow)";
    m_ghostControl(fb, BlinkNow);
}
