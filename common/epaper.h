#pragma once

#include <QObject>

/*!
 * Access to reMarkable's e-paper display controller for the one thing Qt
 * cannot express: forcing a full ("blink") refresh.
 *
 * Why this is needed: the panel writes white->black with a fast partial
 * waveform, but that waveform cannot drive black back to white. Erasing ink
 * in the scene graph therefore leaves the strokes visible on the panel even
 * though the app has painted white over them. Only a full refresh clears it.
 *
 * The entry points live in libqsgepaper.so, which is already loaded in-process
 * when running with `-platform epaper`, so the symbols are resolved with
 * dlsym() rather than by linking against a plugin. Everything degrades to a
 * silent no-op when they are absent (offscreen/software test runs).
 */
class Epaper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit Epaper(QObject *parent = nullptr);

    /*! Resolution is lazy, so this is only meaningful after blink(). */
    bool available() const { return m_ghostControl && m_instance; }

public slots:
    /*! Full-screen flashing refresh, immediately. Must be called AFTER the
     *  new content has actually been rendered, or it flashes the old frame. */
    void blink();

    /*! Ask the controller to do a full refresh on the next update. Call this
     *  BEFORE changing content. */
    void blinkLater();

private:
    // EPFramebuffer::GhostControlMode. Verified against the shipped library:
    // ghostControl() accepts 0, 1 and 3; 0 and 3 share a code path.
    enum GhostControlMode { BlinkNow = 0, BlinkLater = 1, FactoryReset = 3 };

    void resolve();

    bool m_resolved = false;

    void *(*m_instance)() = nullptr;
    void (*m_ghostControl)(void *self, int mode) = nullptr;
    bool m_warned = false;
};
