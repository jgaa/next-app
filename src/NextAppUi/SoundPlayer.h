#pragma once

#include <mutex>
#include <map>

#include <QObject>
#include <QVariantList>

#include "miniaudio.h"

class SoundPlayer
{
    //Q_OBJECT

    struct AudioContext {
        ma_decoder decoder;
        ma_device device;
    };
public:
    enum class State {
        UNINITIALIZED,
        INITIALIZED,
        CLOSED
    };

    SoundPlayer();

    void playSound(const QString& resourcePath, double volume);
    void setState(State state) {
        state_ = state;
    }

    State state() const noexcept {
        return state_;
    }

    ma_engine& engine() noexcept {
        return engine_;
    }

    ~SoundPlayer();

    void close();
    QVariantList outputDevices();
    void applyOutputDevice();

    QByteArrayView getSoundData(const QString& resourcePath);

    static SoundPlayer& instance() noexcept;

private:
    bool context_initialized_{false};
    bool resource_manager_initialized_{false};
    ma_context context_{};
    QString output_device_;
    State state_{State::UNINITIALIZED};
    ma_vfs *vfs_{};
    std::map<QString, QByteArray> sounds_;
    ma_resource_manager resource_manager_{};
    ma_engine engine_{};
    std::mutex mutex_;
};
