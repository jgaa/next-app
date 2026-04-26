#include <QQmlComponent>
#include <QQmlProperty>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>

#include "DualView.h"
#include "NextAppCore.h"
#include "logging.h"
#include "util.h"


using namespace std;

namespace {
void logChildItems(string_view name, const QQuickItem *item)
{
    LOG_TRACE_N << "Children of " << name;
    for (auto *child : item->childItems()) {
        LOG_TRACE_N << "  Child of " << item->objectName() << ": " << child->objectName()
                  << " enabled=" << child->isEnabled()
                  << " visible=" << child->isVisible()
                  << " x=" << child->x() << " y=" << child->y()
                  << " width=" << child->width()
                  << " height=" << child->height();
    }
}

constexpr auto names = to_array<string_view>({
    "Actions",
    "Lists",
    "WorkSessions",
    "Calendar",
    "None"
});

constexpr auto kPersistSplitKey = "UI/mobile/persistDualViewSplit";
constexpr auto kSplitRatioKey = "UI/mobile/dualViewSplitRatio";
constexpr qreal kDefaultSplitRatio = 0.5;
constexpr qreal kMinSplitHeight = 120.0;
constexpr qreal kMinSplitRatio = 0.15;
constexpr qreal kMaxSplitRatio = 0.85;

} // ns

ostream& operator << (ostream& os, DualView::ViewType type)
{
    return os << names.at(static_cast<size_t>(type));
}

DualView::DualView(QQuickItem *parent)
    : DualView(*NextAppCore::instance(), parent)
{
}

DualView::DualView(RuntimeServices& runtime, QQuickItem *parent)
    : QQuickItem{parent}
    , runtime_{runtime}
{

    setObjectName("dualView");

    view_paths_.at(static_cast<size_t>(ViewType::Actions)) = "qrc:/qt/qml/NextAppUi/qml/android/TodoList.qml";
    view_paths_.at(static_cast<size_t>(ViewType::Lists)) = "qrc:/qt/qml/NextAppUi/qml/MainTree.qml";
    view_paths_.at(static_cast<size_t>(ViewType::WorkSessions)) = "qrc:/qt/qml/NextAppUi/qml/android/WorkSessionsStacked.qml";
    view_paths_.at(static_cast<size_t>(ViewType::Calendar)) = "qrc:/qt/qml/NextAppUi/qml/calendar/CalendarView.qml";
    if (runtime_.settings().value(kPersistSplitKey, false).toBool()) {
        split_ratio_ = std::clamp(runtime_.settings().value(kSplitRatioKey, kDefaultSplitRatio).toDouble(),
                                  kMinSplitRatio, kMaxSplitRatio);
    }

    connect(this, &QQuickItem::heightChanged, [this] {
        LOG_TRACE << "DualView height changed to" << height();
        if (split_view_) {
            recalculateLayout();
        }
    });

    connect(this, &QQuickItem::widthChanged, [this] {
        LOG_TRACE << "DualView width changed to" << width();
        if (split_view_) {
            recalculateLayout();
        }
    });

    connect(this, &QQuickItem::visibleChanged, [this] {
        LOG_TRACE << "DualView visible changed to" << isVisible();
    });

    init();
}

void DualView::setViews(DualView::ViewType first, DualView::ViewType second)
{
    LOG_DEBUG << "Setting views: first=" << first << " second=" << second;
    // Clear the scene
    for(auto *v : views_) {
        if (v) {
            v->setVisible(false);
            //v->setParentItem(nullptr);
        }
    }

    logChildItems("this", this);

    if (first == ViewType::None) {
        return;
    }

    ScopedExit emitChanged{ [this] {
        emit viewsChanged();
    }};

    if (auto v = getView(first)) {
        views_.at(static_cast<size_t>(first)) = v;
        first_ = v;
    }

    if (second != ViewType::None && second != first) {
        if (auto v = getView(second)) {
            views_.at(static_cast<size_t>(second)) = v;
            second_ = v;
        }
    } else {
        second_ = {};
    }

    const auto row_height = (height() / (second_ ? 2 : 1)) - 12;

    first_->setParentItem(this);
    first_->setVisible(true);

    if (second_) {
        second_->setParentItem(this);
        second_->setVisible(true);    }

    split_view_->setVisible(first_ && second_);

    logChildItems("this", this);
    recalculateLayout();
}

void DualView::setSplitRatio(qreal ratio)
{
    const auto clamped = std::clamp(ratio, kMinSplitRatio, kMaxSplitRatio);
    if (std::abs(split_ratio_ - clamped) < 0.0001) {
        return;
    }

    split_ratio_ = clamped;
    emit splitRatioChanged();
    recalculateLayout();
    persistSplitRatio();
}

void DualView::setSplitPosition(qreal y)
{
    if (!second_ || !split_view_) {
        return;
    }

    const auto available = height() - split_view_->height();
    if (available <= 0) {
        return;
    }

    setSplitRatio(y / available);
}

void DualView::adjustSplitBy(qreal deltaY)
{
    if (!second_ || !split_view_) {
        return;
    }

    const auto available = height() - split_view_->height();
    if (available <= 0) {
        return;
    }

    setSplitRatio(split_ratio_ + (deltaY / available));
}

QQuickItem *DualView::createView(ViewType type) {
    const auto index = static_cast<size_t>(type);
    QQmlComponent component{&runtime_.qmlEngine(), view_paths_.at(index)};

    if (component.isError()) {
        LOG_ERROR << "Failed to create component for" << view_paths_.at(index);
        return nullptr;
    }

    LOG_DEBUG_N << "Creating view " << names.at(index);

    auto item = qobject_cast<QQuickItem*>(component.create());
    item->setParent(this);
    item->setObjectName(names.at(index));

    if (type == ViewType::Calendar) {
        QQmlProperty::write(item, "primaryForActionList", true);
    }

    return item;
}

QQuickItem *DualView::getView(ViewType type) {
    if (type == ViewType::None) {
        return nullptr;
    }

    if (auto v = views_.at(static_cast<size_t>(type))) {
        return v;
    }

    return createView(type);
}

void DualView::init()
{
    QQmlComponent component{&runtime_.qmlEngine(), "qrc:/qt/qml/NextAppUi/qml/SplitViewComponent.qml"};
    if (auto *sw = component.create()) {
        split_view_ = qobject_cast<QQuickItem*>(sw);
        split_view_->setObjectName("splitView");
        split_view_->setParentItem(this);
        split_view_->setParent(this);
        split_view_->setVisible(false);
    } else {
        LOG_ERROR << "Failed to create SplitViewComponent";
        return;
    }

    logChildItems("this", this);
    logChildItems("split_view", split_view_);

    setViews(ViewType::Actions, ViewType::None);
}

void DualView::recalculateLayout()
{

    if (!height() || !width()) {
        return;
    }

    if (!first_) {
        return;
    }

    auto first_h = height();
    auto second_h = 0.0;
    if (second_) {
        const auto available = height() - split_view_->height();
        if (available <= 0) {
            return;
        }

        const auto min_h = std::min(kMinSplitHeight, available / 2.0);
        first_h = std::clamp(available * split_ratio_, min_h, available - min_h);
        second_h = available - first_h;
    }

    first_->setX(0);
    first_->setY(0);
    first_->setWidth(width());
    first_->resetHeight();
    first_->setHeight(first_h);
    assert(first_->height() == first_h);

    if (second_) {
        split_view_->setX(0);
        split_view_->setY(first_h);
        split_view_->setWidth(width());
        split_view_->setZ(1000);

        second_->setX(0);
        second_->setY(first_h + split_view_->height());
        second_->setWidth(width());
        second_->setHeight(second_h);
        assert(second_->height() == second_h);
    }

    logChildItems("this (after layout)", this);
}

void DualView::persistSplitRatio() const
{
    if (!runtime_.settings().value(kPersistSplitKey, false).toBool()) {
        return;
    }

    runtime_.settings().setValue(kSplitRatioKey, split_ratio_);
}
