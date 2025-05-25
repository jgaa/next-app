#include "CategoryUseModel.h"

#include <span>
#include "logging.h"

using namespace std;


CategoryUseModel::CategoryUseModel(list_fn_t fn, QObject *parent)
: QAbstractListModel(parent), list_fn_(std::move(fn))
{
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

void CategoryUseModel::listChanged()
{
    assert(list_fn_ && "list_fn_ must be set to update the model");

    auto list = list_fn_();

    beginResetModel();
    list_ = std::move(list);
    endResetModel();
}

QPieSeries *CategoryUseModel::pieSeries() {
    if (!pie_series_) {
        pie_series_ = new QPieSeries(this);
        updatePieList();
    }
    return pie_series_;
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
    case Qt::DisplayRole: {
        switch (index.column()) {
            case 0: return item.name;
            case 1: return item.minutes;
            }
        }
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

void CategoryUseModel::updatePieList()
{
    pie_series_->clear();
    if (!list_.empty()) {
        const auto range = std::span{list_}.subspan(0, list_.size() - 1);
        for (const auto &item : range) {
            auto *slice = pie_series_->append(item.name, item.minutes);
            slice->setColor(item.color);
            slice->setLabelVisible(false);
        }
        pie_series_->setHoleSize(0.6);
        pie_series_->setPieSize(1);
    }
    emit pieSeriesChanged();
}
