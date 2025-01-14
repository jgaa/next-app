#include "CategoryUseModel.h"

#include "logging.h"

using namespace std;


CategoryUseModel::CategoryUseModel(QObject *parent) {

    LOG_TRACE_N << "Created " << static_cast<const void*>(this);
}

CategoryUseModel::~CategoryUseModel()
{
    LOG_TRACE_N << "Destroyed " << static_cast<const void*>(this);
}

void CategoryUseModel::setList(const list_t &list) {
    beginResetModel();
    list_ = list;
    endResetModel();
}

int CategoryUseModel::rowCount(const QModelIndex &parent) const
{
    return list_.size();
}

QVariant CategoryUseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= list_.size()) {
        return {};
    }

    const auto& item = list_.at(index.row());

    switch (role) {
        case NameRole:
            return item.name;
        case ColorRole:
            return item.color;
        case MinutesRole:
            return item.minutes;
    }

    return {};
}

QHash<int, QByteArray> CategoryUseModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ColorRole, "colorName"},
        {MinutesRole, "minutes"}
    };

}
