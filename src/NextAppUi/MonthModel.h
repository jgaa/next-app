#pragma once

#include <QObject>
#include <QQmlEngine>

class MonthModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<QString> colors READ getColors NOTIFY colorsChanged)
public:
    explicit MonthModel(unsigned year, unsigned month, QObject *parent = nullptr);

    QList<QString> getColors();
    void setColors(QList<QString> colors);

signals:
    void colorsChanged();

private:
    QList<QString> colors_;
    const unsigned year_;
    const unsigned month_;
};
