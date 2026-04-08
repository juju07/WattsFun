#include "stravauploader.h"
#ifdef STRAVA_ENABLED
#include "tcxexporter.h"

#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QSettings>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>

static constexpr quint16 STRAVA_REDIRECT_PORT = 9876;

// ── construction ──────────────────────────────────────────────────────────────

StravaUploader::StravaUploader(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    loadState();
}

// ── public queries ────────────────────────────────────────────────────────────

bool StravaUploader::hasCredentials() const
{
    return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

bool StravaUploader::isAuthenticated() const
{
    if (m_accessToken.isEmpty()) return false;
    // Valid access token that has not expired yet
    if (!m_tokenExpiry.isNull() && m_tokenExpiry > QDateTime::currentDateTimeUtc())
        return true;
    // Expired but we hold a refresh token
    return !m_refreshToken.isEmpty();
}

// ── credentials ───────────────────────────────────────────────────────────────

void StravaUploader::setCredentials(const QString &clientId, const QString &clientSecret)
{
    m_clientId     = clientId;
    m_clientSecret = clientSecret;
    QSettings s;
    s.setValue("strava/clientId",     clientId);
    s.setValue("strava/clientSecret", clientSecret);
}

// ── OAuth2 flow ───────────────────────────────────────────────────────────────

void StravaUploader::authenticate()
{
    if (!hasCredentials()) {
        emit authFailed(tr("No Strava credentials configured."));
        return;
    }

    // Delete any previous flow instance
    if (m_oauth) {
        m_oauth->deleteLater();
        m_oauth = nullptr;
    }

    m_oauth = new QOAuth2AuthorizationCodeFlow(this);
    m_oauth->setClientIdentifier(m_clientId);
    m_oauth->setClientIdentifierSharedKey(m_clientSecret);
    m_oauth->setAuthorizationUrl(QUrl("https://www.strava.com/oauth/authorize"));
    m_oauth->setAccessTokenUrl(QUrl("https://www.strava.com/oauth/token"));
    m_oauth->setScope("activity:write,read");

    // Local HTTP server catches the redirect
    auto *handler = new QOAuthHttpServerReplyHandler(STRAVA_REDIRECT_PORT, m_oauth);
    m_oauth->setReplyHandler(handler);

    // Open the browser once the URL is ready
    connect(m_oauth, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser,
            this, [](const QUrl &url) {
                QDesktopServices::openUrl(url);
            });

    connect(m_oauth, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        m_accessToken  = m_oauth->token();
        const QVariantMap extra = m_oauth->extraTokens();
        m_refreshToken = extra.value("refresh_token").toString();
        const qint64 expiresAt = extra.value("expires_at").toLongLong();
        if (expiresAt > 0)
            m_tokenExpiry = QDateTime::fromSecsSinceEpoch(expiresAt, Qt::UTC);
        else
            m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(21600);
        saveTokens();
        emit authenticated();

        if (m_hasPending) {
            m_hasPending = false;
            doUpload();
        }
    });

    connect(m_oauth, &QAbstractOAuth::requestFailed,
            this, [this](QAbstractOAuth::Error) {
                emit authFailed(tr("Strava authentication failed. "
                                   "Check your client credentials and try again."));
            });

    m_oauth->grant();
}

// ── upload entry point ────────────────────────────────────────────────────────

void StravaUploader::upload(const DashboardWidget::WorkoutSummary &summary,
                             const QVector<DashboardWidget::WorkoutSample> &samples)
{
    m_pendingSummary = summary;
    m_pendingSamples = samples;
    m_hasPending     = true;

    const bool tokenValid = !m_accessToken.isEmpty()
                            && !m_tokenExpiry.isNull()
                            && m_tokenExpiry > QDateTime::currentDateTimeUtc();

    if (tokenValid) {
        m_hasPending = false;
        doUpload();
    } else if (!m_refreshToken.isEmpty()) {
        refreshAccessToken();   // calls doUpload on success
    } else {
        authenticate();         // full browser flow, calls doUpload on success
    }
}

// ── token refresh ─────────────────────────────────────────────────────────────

void StravaUploader::refreshAccessToken()
{
    emit uploadProgress(tr("Refreshing Strava token…"));

    QNetworkRequest req(QUrl("https://www.strava.com/oauth/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("client_id",     m_clientId);
    body.addQueryItem("client_secret", m_clientSecret);
    body.addQueryItem("refresh_token", m_refreshToken);
    body.addQueryItem("grant_type",    "refresh_token");

    auto *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Refresh failed – fall back to full browser auth
            authenticate();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_accessToken  = obj["access_token"].toString();
        m_refreshToken = obj["refresh_token"].toString();
        const qint64 expiresAt = static_cast<qint64>(obj["expires_at"].toDouble());
        if (expiresAt > 0)
            m_tokenExpiry = QDateTime::fromSecsSinceEpoch(expiresAt, Qt::UTC);
        saveTokens();

        if (m_hasPending) {
            m_hasPending = false;
            doUpload();
        }
    });
}

// ── actual upload ─────────────────────────────────────────────────────────────

void StravaUploader::doUpload()
{
    emit uploadProgress(tr("Generating TCX file…"));
    const QByteArray tcxData = TcxExporter::generate(m_pendingSummary, m_pendingSamples);

    emit uploadProgress(tr("Uploading to Strava…"));

    QNetworkRequest req(QUrl("https://www.strava.com/api/v3/uploads"));
    req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addField = [&](const QByteArray &name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
        part.setBody(value);
        multiPart->append(part);
    };

    addField("data_type",  "tcx");
    addField("sport_type", "VirtualRide");

    // Title: "WattsFun - <mode>"
    const QString title = QStringLiteral("WattsFun - ") +
        (m_pendingSummary.trainingModeName.isEmpty()
         ? QStringLiteral("Free Ride")
         : m_pendingSummary.trainingModeName);
    addField("name", title.toUtf8());

    // Description: program/route name for Interval Training and Map Ride
    if (!m_pendingSummary.programName.isEmpty())
        addField("description", m_pendingSummary.programName.toUtf8());

    // TCX file part
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       "form-data; name=\"file\"; filename=\"workout.tcx\"");
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    filePart.setBody(tcxData);
    multiPart->append(filePart);

    auto *reply = m_nam->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFinished(false,
                tr("Upload failed: %1\n%2")
                    .arg(reply->errorString(), QString::fromUtf8(body)));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const qint64 uploadId = static_cast<qint64>(obj["id"].toDouble());
        const qint64 activityId = static_cast<qint64>(obj["activity_id"].toDouble());

        if (activityId > 0) {
            // Activity already processed
            emit uploadFinished(true,
                tr("Activity uploaded to Strava."), activityId);
        } else if (uploadId > 0) {
            // Need to poll for activity ID
            emit uploadProgress(tr("Activity submitted, waiting for processing…"));
            pollUploadStatus(uploadId);
        } else {
            emit uploadFinished(true,
                tr("Activity submitted to Strava.\nStatus: %1")
                    .arg(obj["status"].toString()));
        }
    });
}

// ── poll for activity ID ──────────────────────────────────────────────────────

void StravaUploader::pollUploadStatus(qint64 uploadId, int attempt)
{
    if (attempt >= 10) {
        // Give up polling, report success without activity ID
        emit uploadFinished(true,
            tr("Activity submitted to Strava (processing may still be in progress)."));
        return;
    }

    QNetworkRequest req(QUrl(
        QString("https://www.strava.com/api/v3/uploads/%1").arg(uploadId)));
    req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, uploadId, attempt]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFinished(true,
                tr("Activity submitted but could not verify status."));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const qint64 activityId = static_cast<qint64>(obj["activity_id"].toDouble());
        const QString error = obj["error"].toString();

        if (!error.isEmpty()) {
            emit uploadFinished(false, tr("Strava processing error: %1").arg(error));
        } else if (activityId > 0) {
            emit uploadFinished(true,
                tr("Activity uploaded to Strava!"), activityId);
        } else {
            // Still processing — retry after 3 seconds
            QTimer::singleShot(3000, this, [this, uploadId, attempt]() {
                pollUploadStatus(uploadId, attempt + 1);
            });
        }
    });
}

// ── persistence ───────────────────────────────────────────────────────────────

void StravaUploader::loadState()
{
    QSettings s;
    m_clientId     = s.value("strava/clientId").toString();
    m_clientSecret = s.value("strava/clientSecret").toString();
    m_accessToken  = s.value("strava/accessToken").toString();
    m_refreshToken = s.value("strava/refreshToken").toString();
    const qint64 exp = s.value("strava/tokenExpiry", 0LL).toLongLong();
    if (exp > 0)
        m_tokenExpiry = QDateTime::fromSecsSinceEpoch(exp, Qt::UTC);
}

void StravaUploader::saveTokens()
{
    QSettings s;
    s.setValue("strava/accessToken",  m_accessToken);
    s.setValue("strava/refreshToken", m_refreshToken);
    s.setValue("strava/tokenExpiry",  m_tokenExpiry.toSecsSinceEpoch());
}

#endif // STRAVA_ENABLED
