#pragma once

#include <QQmlEngine>
#include <QAbstractListModel>
#include <QPieSeries>

/*! Model to to list categories used and how many minutes they were used.
 *
 * This model is used to display the categories used and how many minutes they were used,
 * typically for a calendar day.
 */



class CategoryUseModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QPieSeries* pieSeries READ pieSeries NOTIFY pieSeriesChanged)

public:
    struct Data {
        QString name;
        QString color;
        uint minutes;
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        ColorRole,
        MinutesRole
    };

    using list_t = std::vector<Data>;
    using list_fn_t = std::function<list_t()>;

    CategoryUseModel(list_fn_t fn, QObject *parent = {});
    ~CategoryUseModel();

    void setList(const list_t& list);
    void listChanged();
    QPieSeries* pieSeries() ;

signals:
    void pieSeriesChanged();

private:
    list_t list_;
    list_fn_t list_fn_;
    QPieSeries *pie_series_{};

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void updatePieList();
};
