#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>

class DbStore;
class QQmlEngine;

#include "ServerCommAccess.h"

class SettingsAccess
{
public:
    virtual ~SettingsAccess() = default;

    virtual QVariant value(const QString& key, const QVariant& default_value = {}) const = 0;
    virtual void setValue(const QString& key, const QVariant& value) = 0;
    virtual void remove(const QString& key) = 0;
    virtual void sync() = 0;
};

class QSettingsAccess final : public SettingsAccess
{
public:
    QVariant value(const QString& key, const QVariant& default_value = {}) const override
    {
        return QSettings{}.value(key, default_value);
    }

    void setValue(const QString& key, const QVariant& value) override
    {
        QSettings{}.setValue(key, value);
    }

    void remove(const QString& key) override
    {
        QSettings{}.remove(key);
    }

    void sync() override
    {
        QSettings{}.sync();
    }
};

class RuntimeServices
{
public:
    virtual ~RuntimeServices() = default;

    virtual DbStore& db() const noexcept = 0;
    virtual ServerCommAccess& serverComm() const noexcept = 0;
    virtual SettingsAccess& settings() const noexcept = 0;
    virtual QVariant appProperty(const QString& name) const noexcept = 0;
    virtual void setAppProperty(const QString& name, const QVariant& value) = 0;
    virtual void playSound(double volume, const QString& sound_file) = 0;
    virtual void setOnline(bool online) = 0;
    virtual void showSyncPopup(bool visible) = 0;
    virtual void setPlansEnabled(bool enable) = 0;
    virtual QCoro::Task<void> onAccountDeleted() = 0;
    virtual void showUnrecognizedDeviceError() = 0;
    virtual QObject& appEventSource() noexcept = 0;
    virtual QQmlEngine& qmlEngine() const noexcept = 0;
    virtual bool isMobileUi() const noexcept = 0;
};
