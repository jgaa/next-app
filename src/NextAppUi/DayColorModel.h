#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUuid>

#include "nextapp.qpb.h"

class DayColorModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList names READ getNames NOTIFY colorsChanged)
public:
    explicit DayColorModel(QObject *parent = nullptr);

    // Return a list of the names in the same order as we have them stored
    // Then we can use the indes to get the full record.
    QStringList getNames() const;

    Q_INVOKABLE QUuid getUuid(int index);

    Q_INVOKABLE int getIndexForColorUuid(const QString& uuid);

    void dayColorsChanged(const nextapp::pb::DayColorDefinitions& defs);

signals:
    void colorsChanged();

private:
    void setDefinitions();

    nextapp::pb::DayColorRepeated daycolors_;
};
