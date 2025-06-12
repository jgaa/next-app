#pragma once

#include <QQmlEngine>
#include "qcorotask.h"

class ImportExportModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool working MEMBER working_ NOTIFY workingChanged)
    Q_PROPERTY(QUrl dataPath MEMBER data_path_ CONSTANT)

public:
    ImportExportModel();

    Q_INVOKABLE void exportData(const QUrl& fileName);

signals:
    void workingChanged();

private:
    void setWorking(bool working);

    QCoro::Task<void> doExport(QString fileName);
    QCoro::Task<void> doImport(QString fileName);

    bool working_{false};
    QUrl data_path_;
};
