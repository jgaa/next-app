#pragma once

#include <QObject>
#include <QQmlEngine>

class MonthModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool validColors READ getColorsIsValid NOTIFY colorsChanged)
public:
    explicit MonthModel(unsigned year, unsigned month, QObject *parent = nullptr);

    void setColors(QList<QUuid> uuids);

    bool getColorsIsValid() const {
        return valid_;
    }

    // Day is index 1 = 31
    Q_INVOKABLE QString getColorForDayInMonth(int day);

    Q_INVOKABLE QString getUuidForDayInMonth(int day);

signals:
    void colorsChanged();

private:
    bool valid_{false};
    QList<QUuid> uuids_;
    const unsigned year_;
    const unsigned month_;
};
