#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QQmlEngine>

class UseCaseTemplates : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    struct List {
        enum Kind {
            FOLDER,
            ORGANIZATION,
            PERSON,
            PROJECT,
            TASK
        };

        QString name;
        QString description;
        Kind kind;
        QList<List> children;
    };

    struct UseCaseTemplate
    {
        QString name;
        QString description;
        QList<List> lists;
    };

    UseCaseTemplates();

    Q_INVOKABLE QStringList getTemplateNames() const noexcept;
    Q_INVOKABLE void createFromTemplate(int index);
    Q_INVOKABLE QString getDescription(int index);

private:
    QList<UseCaseTemplate> templates_;
};
