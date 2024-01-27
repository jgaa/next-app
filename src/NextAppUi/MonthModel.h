#pragma once

#include <QObject>
#include <QQmlEngine>

class DaysModel;

// MVC via server
class MonthModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool validColors READ getColorsIsValid NOTIFY colorsChanged)
public:
    explicit MonthModel(unsigned year, unsigned month, DaysModel& parent);

    bool getColorsIsValid() const {
        return valid_;
    }

    // Day is index 1 = 31
    Q_INVOKABLE QString getColorForDayInMonth(int day);
    Q_INVOKABLE QString getUuidForDayInMonth(int day);

signals:
    void colorsChanged();

public slots:
    void updatedMonth(int year, int month);

private:
    bool valid_ = false;
    const unsigned year_;
    const unsigned month_;
    DaysModel& parent_;
};
