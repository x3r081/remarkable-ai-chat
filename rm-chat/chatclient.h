#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>

/*!
 * Minimal OpenAI-compatible chat client (POST {base}/chat/completions,
 * Bearer auth, non-streaming). Handwriting goes in as a base64 PNG in the
 * standard image_url content shape, so any vision-capable model works.
 */
class ChatClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit ChatClient(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    /*! history: array of {role, content} text-only objects (oldest first).
     *  imagePngBase64 xor typedText carries the new user message. */
    Q_INVOKABLE void send(const QString &apiBase, const QString &apiKey,
                          const QString &model, const QString &systemPrompt,
                          const QJsonArray &history,
                          const QString &imagePngBase64,
                          const QString &typedText);

    /*! GET {base}/models - cheap credentials check. */
    Q_INVOKABLE void testConnection(const QString &apiBase, const QString &apiKey);

signals:
    void busyChanged();
    /*! transcription is non-empty when the model prefixed its reply with the
     *  Read: "..." line we ask for in the system prompt. */
    void replyReady(const QString &text, const QString &transcription);
    void failed(const QString &error);
    void testResult(bool ok, const QString &detail);

private:
    void setBusy(bool b);
    QNetworkRequest makeRequest(const QString &apiBase, const QString &path,
                                const QString &apiKey) const;

    QNetworkAccessManager m_nam;
    bool m_busy = false;
};
