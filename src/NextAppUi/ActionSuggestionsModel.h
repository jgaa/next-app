#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <optional>
#include "RuntimeServices.h"

class ActionSuggestionsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY suggestionsChanged)
public:
    explicit ActionSuggestionsModel(QObject* parent = nullptr);
    ActionSuggestionsModel(RuntimeServices& runtime, QObject* parent = nullptr);
    QVariantList suggestions() const { return suggestions_; }
    bool busy() const { return busy_; }
    QString error() const { return error_; }

    // state: exhausted..exceptional (0..4); scope: scheduled active/unset/both (0..2).
    // listScope: all/selected list/selected list and descendants (0..2); empty node means all.
    // restricted=true limits candidates to the supplied action-list selection, even if empty.
    Q_INVOKABLE void suggest(int state, int minutes, const QStringList& categories,
                             int scope, bool restricted, const QStringList& ids,
                             int listScope = 0, const QString& node = {});
    Q_INVOKABLE bool createTimeBox(const QString& uuid, qint64 start, int minutes);
    static std::optional<double> rank(const nextapp::pb::Action& action, int state,
                                      int minutes, int scope, qint64 now);
    static int timeBoxMinutes(int estimate, int available,
                              const nextapp::pb::UserGlobalSettings& settings);
signals:
    void suggestionsChanged();
    void busyChanged();
private:
    QCoro::Task<void> load(int state, int minutes, QStringList categories,
                          int scope, bool restricted, QStringList ids, int listScope, QString node);
    RuntimeServices& runtime_;
    QVariantList suggestions_;
    QString error_;
    bool busy_ = false;
};
