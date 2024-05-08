#pragma once

#include <QQmlEngine>

class MaterialDesignStyling : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString primary READ primary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onPrimary READ onPrimary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString primaryContainer READ primaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onPrimaryContainer READ onPrimaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString primaryFixed READ primaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onPrimaryFixed READ onPrimaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString primaryFixedDim READ primaryFixedDim NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onPrimaryFixedVariant READ onPrimaryFixedVariant NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString secondary READ secondary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onSecondary READ onSecondary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString secondaryContainer READ secondaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onSecondaryContainer READ onSecondaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString secondaryFixed READ secondaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onSecondaryFixed READ onSecondaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString secondaryFixedDim READ secondaryFixedDim NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onSecondaryFixedVariant READ onSecondaryFixedVariant NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString tertiary READ tertiary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onTertiary READ onTertiary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString tertiaryContainer READ tertiaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onTertiaryContainer READ onTertiaryContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString tertiaryFixed READ tertiaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onTertiaryFixed READ onTertiaryFixed NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString tertiaryFixedDim READ tertiaryFixedDim NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onTertiaryFixedVariant READ onTertiaryFixedVariant NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString error READ error NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onError READ onError NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString errorContainer READ errorContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onErrorContainer READ onErrorContainer NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString surfaceDim READ surfaceDim NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surface READ surface NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surfaceBright READ surfaceBright NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString surfaceContainerLowest READ surfaceContainerLowest NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surfaceContainerLow READ surfaceContainerLow NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surfaceContainer READ surfaceContainer NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surfaceContainerHigh READ surfaceContainerHigh NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString surfaceContainerHighest READ surfaceContainerHighest NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString onSurface READ onSurface NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString onSurfaceVariant READ onSurfaceVariant NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString outline READ outline NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString outlineVariant READ outlineVariant NOTIFY colorsChanged FINAL)

    Q_PROPERTY(QString inverseSurface READ inverseSurface NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString inverseOnSurface READ inverseOnSurface NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString inversePrimary READ inversePrimary NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString scrim READ scrim NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString shadow READ shadow NOTIFY colorsChanged FINAL)

    struct ColorTheme {
        QString primary;
        QString onPrimary;
        QString primaryContainer;
        QString onPrimaryContainer;
        QString primaryFixed;
        QString onPrimaryFixed;
        QString primaryFixedDim;
        QString onPrimaryFixedVariant;

        QString secondary;
        QString onSecondary;
        QString secondaryContainer;
        QString onSecondaryContainer;
        QString secondaryFixed;
        QString onSecondaryFixed;
        QString secondaryFixedDim;
        QString onSecondaryFixedVariant;

        QString tertiary;
        QString onTertiary;
        QString tertiaryContainer;
        QString onTertiaryContainer;
        QString tertiaryFixed;
        QString onTertiaryFixed;
        QString tertiaryFixedDim;
        QString onTertiaryFixedVariant;

        QString error;
        QString onError;
        QString errorContainer;
        QString onErrorContainer;

        QString surfaceDim;
        QString surface;
        QString surfaceBright;

        QString surfaceContainerLowest;
        QString surfaceContainerLow;
        QString surfaceContainer;
        QString surfaceContainerHigh;
        QString surfaceContainerHighest;

        QString onSurface;
        QString onSurfaceVariant;
        QString outline;
        QString outlineVariant;

        QString inverseSurface;
        QString inverseOnSurface;
        QString inversePrimary;
        QString scrim;
        QString shadow;
    };

public:
    MaterialDesignStyling();

    QString primary() const;
    QString onPrimary() const;
    QString primaryContainer() const;
    QString onPrimaryContainer() const;
    QString primaryFixed() const;
    QString onPrimaryFixed() const;
    QString primaryFixedDim() const;
    QString onPrimaryFixedVariant() const;

    QString secondary() const;
    QString onSecondary() const;
    QString secondaryContainer() const;
    QString onSecondaryContainer() const;
    QString secondaryFixed() const;
    QString onSecondaryFixed() const;
    QString secondaryFixedDim() const;
    QString onSecondaryFixedVariant() const;

    QString tertiary() const;
    QString onTertiary() const;
    QString tertiaryContainer() const;
    QString onTertiaryContainer() const;
    QString tertiaryFixed() const;
    QString onTertiaryFixed() const;
    QString tertiaryFixedDim() const;
    QString onTertiaryFixedVariant() const;

    QString error() const;
    QString onError() const;
    QString errorContainer() const;
    QString onErrorContainer() const;

    QString surfaceDim() const;
    QString surface() const;
    QString surfaceBright() const;

    QString surfaceContainerLowest() const;
    QString surfaceContainerLow() const;
    QString surfaceContainer() const;
    QString surfaceContainerHigh() const;
    QString surfaceContainerHighest() const;

    QString onSurface() const;
    QString onSurfaceVariant() const;
    QString outline() const;
    QString outlineVariant() const;

    QString inverseSurface() const;
    QString inverseOnSurface() const;
    QString inversePrimary() const;
    QString scrim() const;
    QString shadow() const;

signals:
    void colorsChanged();

private:
    void setLightTheme();
    void setDarkTheme();

    ColorTheme theme_;
};
