#pragma once
#ifdef STRAVA_ENABLED
#include "dashboardwidget.h"

#include <QObject>
#include <QDateTime>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QOAuth2AuthorizationCodeFlow;

// Handles Strava OAuth 2.0 authorisation and activity upload.
//
// Setup (one-time):
//   1. Register a Strava API application at https://www.strava.com/settings/api
//   2. Set the Authorization Callback Domain to "localhost"
//   3. In WattsFun go to Strava → Set API Credentials… and enter the
//      Client ID and Client Secret shown on the Strava API settings page.
//
// Workflow:
//   • Call authenticate() once (opens the browser).
//   • Call upload() after a workout – it re-uses the stored token and
//     refreshes it automatically when needed.
class StravaUploader : public QObject
{
    Q_OBJECT
public:
    explicit StravaUploader(QObject *parent = nullptr);

    // True once client_id / client_secret are stored in QSettings.
    bool hasCredentials() const;

    // True if we have a valid (or auto-refreshable) access token.
    bool isAuthenticated() const;

    // Persist credentials.  They survive across app restarts.
    void setCredentials(const QString &clientId, const QString &clientSecret);

    // Start the browser-based OAuth2 flow.
    void authenticate();

    // Generate a TCX file from the samples and upload to Strava.
    // If reauthentication is needed it happens automatically first.
    void upload(const DashboardWidget::WorkoutSummary &summary,
                const QVector<DashboardWidget::WorkoutSample> &samples);

signals:
    void authenticated();
    void authFailed(const QString &message);
    void uploadProgress(const QString &message);
    void uploadFinished(bool success, const QString &message,
                        qint64 activityId = 0);

private:
    void refreshAccessToken();
    void doUpload();
    void pollUploadStatus(qint64 uploadId, int attempt = 0);
    void loadState();
    void saveTokens();

    QString   m_clientId;
    QString   m_clientSecret;
    QString   m_accessToken;
    QString   m_refreshToken;
    QDateTime m_tokenExpiry;          // UTC

    QNetworkAccessManager          *m_nam   = nullptr;
    QOAuth2AuthorizationCodeFlow   *m_oauth = nullptr;

    // Buffered upload – set before any async token operation so that doUpload()
    // can be called once the token is ready.
    DashboardWidget::WorkoutSummary        m_pendingSummary;
    QVector<DashboardWidget::WorkoutSample> m_pendingSamples;
    bool m_hasPending = false;
};

#endif // STRAVA_ENABLED
