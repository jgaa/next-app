#include <QtCore/QObject>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QJniObject>

struct JNIEnv;

// A simple QObject to emit signals
class AndroidFcmBridge : public QObject {
    Q_OBJECT
public:
    static AndroidFcmBridge &instance();

    QString getToken();
    QString getPackageId();
    void setToken(const QString &token) {
        if (token_ != token) {
            token_ = token;
            emit tokenRefreshed(token_);
        }
    }

signals:
    void tokenRefreshed(const QString &token);
    void messageReceived(const QString &messageId,
                         const QString &notification,
                         const QString &data);

private:
    QString token_;
    QString packageId_;
};

bool registerAndroidFcmBridgeNatives(JNIEnv* env);
