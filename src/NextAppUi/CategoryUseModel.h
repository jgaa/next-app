#pragma once

#include <QQmlEngine>
#include <QAbstractListModel>

/*! Model to to list categories used and how many minutes they were used.
 *
 * This model is used to display the categories used and how many minutes they were used,
 * typically for a calendar day.
 */



class CategoryUseModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    struct Data {
        QString name;
        QString color;
        uint minutes;
        bool isSummary{};
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        ColorRole,
        MinutesRole,
        SummaryRole
    };

    using list_t = std::vector<Data>;
    using list_fn_t = std::function<list_t()>;

    CategoryUseModel(list_fn_t fn, QObject *parent = {});
    ~CategoryUseModel();

    void setList(const list_t& list);
    void listChanged();

private:
    list_t list_;
    list_fn_t list_fn_;

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};
