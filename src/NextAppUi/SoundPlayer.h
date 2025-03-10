#pragma once

#include <QObject>

#include "miniaudio.h"

class SoundPlayer
{
    //Q_OBJECT

    struct AudioContext {
        ma_decoder decoder;
        ma_device device;
    };
public:
    static void playSound(const QString& resourcePath, double volume);

private:
    static void playSoundAsync(QString resourcePath, double volume);
};
