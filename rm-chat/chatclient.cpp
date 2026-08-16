#include "chatclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcChat, "rmchat.api")

namespace {
constexpr int kTimeoutMs = 180 * 1000;

QString joinUrl(const QString &base, const QString &path)
{
    QString b = base.trimmed();
    while (b.endsWith('/'))
        b.chop(1);
    return b + path;
}
} // namespace

ChatClient::ChatClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setTransferTimeout(kTimeoutMs);
}

void ChatClient::setBusy(bool b)
{
    if (m_busy == b)
        return;
    m_busy = b;
    emit busyChanged();
}

QNetworkRequest ChatClient::makeRequest(const QString &apiBase, const QString &path,
                                        const QString &apiKey) const
{
    QNetworkRequest req(QUrl(joinUrl(apiBase, path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.trimmed().toUtf8());
    return req;
}

void ChatClient::send(const QString &apiBase, const QString &apiKey,
                      const QString &model, const QString &systemPrompt,
                      const QJsonArray &history,
                      const QString &imagePngBase64, const QString &typedText)
{
    if (m_busy)
        return;

    QJsonArray messages;
    if (!systemPrompt.trimmed().isEmpty())
        messages.append(QJsonObject{{"role", "system"},
                                    {"content", systemPrompt}});
    for (const auto &m : history)
        messages.append(m);

    if (!imagePngBase64.isEmpty()) {
        // Standard multimodal shape - works on every OpenAI-compatible
        // provider that supports vision.
        QJsonArray content;
        content.append(QJsonObject{
            {"type", "image_url"},
            {"image_url", QJsonObject{
                {"url", "data:image/png;base64," + imagePngBase64}}}});
        if (!typedText.trimmed().isEmpty())
            content.append(QJsonObject{{"type", "text"}, {"text", typedText}});
        messages.append(QJsonObject{{"role", "user"}, {"content", content}});
    } else {
        messages.append(QJsonObject{{"role", "user"}, {"content", typedText}});
    }

    const QJsonObject body{
        {"model", model.trimmed()},
        {"messages", messages},
        {"stream", false},
    };

    qCInfo(lcChat) << "POST chat/completions model=" << model
                   << "history=" << history.size()
                   << "image=" << !imagePngBase64.isEmpty();
    setBusy(true);

    QNetworkReply *reply = m_nam.post(makeRequest(apiBase, "/chat/completions", apiKey),
                                      QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setBusy(false);

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();

        if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
            qCWarning(lcChat) << "network error:" << reply->errorString();
            emit failed(QString("Network error: %1").arg(reply->errorString()));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        if (status < 200 || status >= 300) {
            QString detail = obj["error"].toObject()["message"].toString();
            if (detail.isEmpty())
                detail = QString::fromUtf8(raw.left(200));
            qCWarning(lcChat) << "http error" << status << detail;
            emit failed(QString("HTTP %1: %2").arg(status).arg(detail));
            return;
        }

        qCInfo(lcChat) << "reply ok, HTTP" << status;
        const QString content = obj["choices"].toArray().first().toObject()
                                    ["message"].toObject()["content"].toString();
        if (content.isEmpty()) {
            emit failed("Empty response from model");
            return;
        }

        // Split off the transcription line the system prompt asks for.
        static const QRegularExpression re(
            "^\\s*Read:\\s*\"([^\"\\n]*)\"[ \\t]*\\n+");
        QString transcription, text = content;
        const auto m = re.match(content);
        if (m.hasMatch()) {
            transcription = m.captured(1).trimmed();
            text = content.mid(m.capturedEnd()).trimmed();
            if (text.isEmpty())
                text = content.trimmed();   // model sent only the Read line
        }
        emit replyReady(text, transcription);
    });
}

void ChatClient::testConnection(const QString &apiBase, const QString &apiKey)
{
    if (m_busy)
        return;
    setBusy(true);

    QNetworkReply *reply = m_nam.get(makeRequest(apiBase, "/models", apiKey));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setBusy(false);

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            emit testResult(true, QString("OK (HTTP %1) - credentials accepted").arg(status));
        } else {
            const QByteArray raw = reply->readAll();
            QString detail = QJsonDocument::fromJson(raw).object()
                                 ["error"].toObject()["message"].toString();
            if (detail.isEmpty())
                detail = reply->errorString();
            emit testResult(false, QString("HTTP %1: %2").arg(status).arg(detail.left(160)));
        }
    });
}
