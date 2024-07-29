#pragma once

#include <QObject>
#include <QQmlEngine>

class OtpModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString otp MEMBER otp_ NOTIFY otpChanged)
    Q_PROPERTY(QString email MEMBER email_ NOTIFY emailChanged)
    Q_PROPERTY(QString error MEMBER error_ NOTIFY errorChanged)

public:
    OtpModel(QObject * parent = nullptr);

    Q_INVOKABLE void requestOtpForNewDevice();

signals:
    void otpChanged();
    void errorChanged();
    void emailChanged();

private:
    QString otp_;
    QString email_;
    QString error_;
};
