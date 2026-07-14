#include "DonutChart.h"

#include <QPainter>

#include <algorithm>

namespace {

constexpr auto fullCircleDegrees = 360.0;
constexpr auto qtAngleFactor = 16.0;

}

DonutChart::DonutChart(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setImplicitWidth(240);
    setImplicitHeight(180);
}

void DonutChart::setModel(QAbstractItemModel *model)
{
    if (model_ == model) {
        return;
    }

    detachModel();
    model_ = model;
    attachModel();
    emit modelChanged();
    emit hasDataChanged();
    update();
}

void DonutChart::setBackgroundColor(const QColor &color)
{
    if (background_color_ == color) {
        return;
    }

    background_color_ = color;
    emit backgroundColorChanged();
    update();
}

void DonutChart::setHoleRatio(qreal ratio)
{
    const auto clamped = std::clamp(ratio, 0.05, 0.95);
    if (qFuzzyCompare(hole_ratio_, clamped)) {
        return;
    }

    hole_ratio_ = clamped;
    emit holeRatioChanged();
    update();
}

bool DonutChart::hasData() const noexcept
{
    if (!model_) {
        return false;
    }

    const auto minutes_role = minutesRole();
    const auto summary_role = summaryRole();
    for (int row = 0; row < model_->rowCount(); ++row) {
        if (model_->index(row, 0).data(summary_role).toBool()) {
            continue;
        }

        if (model_->index(row, 0).data(minutes_role).toDouble() > 0.0) {
            return true;
        }
    }

    return false;
}

void DonutChart::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(boundingRect(), Qt::transparent);

    if (!hasData()) {
        return;
    }

    const QRectF bounds = boundingRect();
    const auto diameter = std::min(bounds.width(), bounds.height());
    if (diameter <= 0.0) {
        return;
    }

    const QRectF pie_rect{
        bounds.center().x() - diameter / 2.0,
        bounds.center().y() - diameter / 2.0,
        diameter,
        diameter
    };

    const auto minutes_role = minutesRole();
    const auto color_role = colorRole();
    const auto summary_role = summaryRole();

    double total_minutes = 0.0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        if (model_->index(row, 0).data(summary_role).toBool()) {
            continue;
        }

        total_minutes += std::max(0.0, model_->index(row, 0).data(minutes_role).toDouble());
    }

    if (total_minutes <= 0.0) {
        return;
    }

    int non_zero_slices = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        if (model_->index(row, 0).data(summary_role).toBool()) {
            continue;
        }

        if (model_->index(row, 0).data(minutes_role).toDouble() > 0.0) {
            ++non_zero_slices;
        }
    }

    painter->setPen(Qt::NoPen);
    double start_angle = 90.0;
    double remaining_angle = fullCircleDegrees;
    int painted_slices = 0;

    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto minutes = std::max(0.0, model_->index(row, 0).data(minutes_role).toDouble());
        if (model_->index(row, 0).data(summary_role).toBool()) {
            continue;
        }

        if (minutes <= 0.0) {
            continue;
        }

        ++painted_slices;
        auto span_angle = fullCircleDegrees * (minutes / total_minutes);
        if (painted_slices == non_zero_slices || remaining_angle - span_angle < 0.0) {
            span_angle = remaining_angle;
        }

        auto color = QColor{model_->index(row, 0).data(color_role).toString()};
        if (!color.isValid() || color.alpha() == 0) {
            color = QColor{"#808080"};
        }

        painter->setBrush(color);
        painter->drawPie(pie_rect, static_cast<int>(start_angle * qtAngleFactor), static_cast<int>(-span_angle * qtAngleFactor));
        start_angle -= span_angle;
        remaining_angle -= span_angle;
    }

    painter->setBrush(background_color_);
    painter->drawEllipse(pie_rect.center(), pie_rect.width() * hole_ratio_ / 2.0, pie_rect.height() * hole_ratio_ / 2.0);
}

void DonutChart::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void DonutChart::attachModel()
{
    if (!model_) {
        return;
    }

    auto refresh_fn = [this](auto&&...) { refresh(); };
    connect(model_, &QAbstractItemModel::modelReset, this, refresh_fn);
    connect(model_, &QAbstractItemModel::dataChanged, this, refresh_fn);
    connect(model_, &QAbstractItemModel::rowsInserted, this, refresh_fn);
    connect(model_, &QAbstractItemModel::rowsRemoved, this, refresh_fn);
    connect(model_, &QAbstractItemModel::layoutChanged, this, refresh_fn);
}

void DonutChart::detachModel()
{
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }
}

void DonutChart::refresh()
{
    emit hasDataChanged();
    update();
}

int DonutChart::colorRole() const
{
    if (!model_) {
        return Qt::DisplayRole;
    }

    const auto roles = model_->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == "colorName") {
            return it.key();
        }
    }

    return Qt::DisplayRole;
}

int DonutChart::minutesRole() const
{
    if (!model_) {
        return Qt::DisplayRole;
    }

    const auto roles = model_->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == "minutes") {
            return it.key();
        }
    }

    return Qt::DisplayRole;
}

int DonutChart::summaryRole() const
{
    if (!model_) {
        return Qt::DisplayRole;
    }

    const auto roles = model_->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == "isSummary") {
            return it.key();
        }
    }

    return Qt::DisplayRole;
}
