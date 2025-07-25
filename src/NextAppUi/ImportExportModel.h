#pragma once

#include <QQmlEngine>
#include <QFile>
#include "qcorotask.h"

class ImportExportModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool working MEMBER working_ NOTIFY workingChanged)
    Q_PROPERTY(bool hasBackup MEMBER have_backup_ NOTIFY fileChanged)
    Q_PROPERTY(bool hasJson MEMBER have_json_ NOTIFY fileChanged)
    Q_PROPERTY(QUrl dataPath MEMBER data_path_ CONSTANT)

public:
    constexpr static uint8_t current_file_version = 1;
    using file_t = std::shared_ptr<QFile>;

#pragma pack(push, 1)
    struct FileHeader
    {
        char magic[7] = {'N', 'e', 'x', 't', 'A', 'p', 'p'};
        uint8_t version = current_file_version;
    };
#pragma pack(pop)

    ImportExportModel();

    Q_INVOKABLE void exportData(const QUrl& url);
    Q_INVOKABLE void exportDataMobile(bool json);
    Q_INVOKABLE void importData(const QUrl& url);
    Q_INVOKABLE void shareDataMobile(bool json);
    Q_INVOKABLE void viewDataMobile(bool json);
    Q_INVOKABLE void deleteDataMobile();

signals:
    void workingChanged();
    void fileChanged();

private:
    void setWorking(bool working);
    void setHaveBackup(bool haveBackup);
    void setHaveJson(bool haveJson);
    void scanFiles();


    QCoro::Task<void> doExport(file_t file);
    QCoro::Task<void> doImport(QString fileName);

    bool working_{false};
    QUrl data_path_;
    bool have_backup_{false};
    bool have_json_{false};
};
