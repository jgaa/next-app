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
    constexpr static uint8_t current_file_version = 1;

#pragma pack(push, 1)
    struct FileHeader
    {
        char magic[7] = {'N', 'e', 'x', 't', 'A', 'p', 'p'};
        uint8_t version = current_file_version;
    };
#pragma pack(pop)

    ImportExportModel();

    Q_INVOKABLE void exportData(const QUrl& url);
    Q_INVOKABLE void importData(const QUrl& url);

signals:
    void workingChanged();

private:
    void setWorking(bool working);

    QCoro::Task<void> doExport(QString fileName);
    QCoro::Task<void> doImport(QString fileName);

    bool working_{false};
    QUrl data_path_;
};
