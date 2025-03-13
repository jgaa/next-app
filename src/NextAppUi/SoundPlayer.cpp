#include "SoundPlayer.h"
#include <QResource>
#include <QByteArray>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>
#include <QThread>

#include "logging.h"
#include "util.h"

namespace {

// Audio callback (fills the buffer)
void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
    if (pDecoder) {
        ma_decoder_read_pcm_frames(pDecoder, pOutput, frameCount, nullptr);
    }
}


} // anon ns

void SoundPlayer::playSound(const QString &resourcePath, double volume)
{
    QString adjusted_path= resourcePath;

    // Remove "qrc:/" prefix if present
    if (adjusted_path.startsWith("qrc:/")) {
        adjusted_path = adjusted_path.mid(3);  // Remove "qrc"
    }

    auto result = QtConcurrent::run(playSoundAsync, adjusted_path, volume);
    Q_UNUSED(result);
}

void SoundPlayer::playSoundAsync(QString resourcePath, double volume)
{

    if (volume < 0.0 || volume > 1.0) return; // Ensure valid volume

    QResource resource(resourcePath);
    if (!resource.isValid()) {
        LOG_ERROR_N << "Resource not found:" << resourcePath;
        return;
    }

    QByteArray soundData(reinterpret_cast<const char*>(resource.data()), resource.size());

    ma_decoder decoder{};
    if (ma_decoder_init_memory(soundData.constData(), soundData.size(), nullptr, &decoder) != MA_SUCCESS) {
        LOG_WARN_N << "Failed to initialize decoder for" << resourcePath;
        return;
    }

    ScopedExit onExit([&decoder](){
        ma_decoder_uninit(&decoder);
    });

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = decoder.outputFormat;
    deviceConfig.playback.channels = decoder.outputChannels;
    deviceConfig.sampleRate = decoder.outputSampleRate;
    deviceConfig.dataCallback = audioCallback;
    deviceConfig.pUserData = &decoder;

    ma_device device{};
    if (ma_device_init(nullptr, &deviceConfig, &device) != MA_SUCCESS) {
        LOG_WARN_N << "Failed to initialize audio device";
        ma_decoder_uninit(&decoder);
        return;
    }

    ScopedExit onExit2([&device]() {
        ma_device_uninit(&device);
    });

    // Set volume (convert 0-100% to 0.0-1.0)
    ma_device_set_master_volume(&device, static_cast<float>(volume));

    ma_device_start(&device);

    // Wait for playback to finish
    LOG_TRACE_N << "Playing sound:" << resourcePath;
    while (ma_device_is_started(&device)) {
        QThread::msleep(50);
    }
    LOG_TRACE_N << "Sound playback finished:" << resourcePath;
}
