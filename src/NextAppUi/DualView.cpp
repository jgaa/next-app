#include <QQmlComponent>
#include <QQmlProperty>
#include <QQmlEngine>

#include "DualView.h"
#include "NextAppCore.h"
#include "logging.h"
#include "util.h"


using namespace std;

namespace {
void logChildItems(string_view name, const QQuickItem *item)
{
    LOG_DEBUG << "Children of " << name;
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

} // ns

ostream& operator << (ostream& os, DualView::ViewType type)
{
    return os << names.at(static_cast<size_t>(type));
}

DualView::DualView(QQuickItem *parent)
    : QQuickItem{parent}
{

    setObjectName("dualView");

    view_paths_.at(static_cast<size_t>(ViewType::Actions)) = "qrc:/qt/qml/NextAppUi/qml/android/TodoList.qml";
    view_paths_.at(static_cast<size_t>(ViewType::Lists)) = "qrc:/qt/qml/NextAppUi/qml/MainTree.qml";
    view_paths_.at(static_cast<size_t>(ViewType::WorkSessions)) = "qrc:/qt/qml/NextAppUi/qml/android/WorkSessionsStacked.qml";
    view_paths_.at(static_cast<size_t>(ViewType::Calendar)) = "qrc:/qt/qml/NextAppUi/qml/calendar/CalendarView.qml";

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

QQuickItem *DualView::createView(ViewType type) {
    const auto index = static_cast<size_t>(type);
    QQmlComponent component{&NextAppCore::instance()->engine(), view_paths_.at(index)};

    if (component.isError()) {
        LOG_ERROR << "Failed to create component for" << view_paths_.at(index);
        return nullptr;
    }

    LOG_DEBUG_N << "Creating view " << names.at(index);

    auto item = qobject_cast<QQuickItem*>(component.create());
    item->setParent(this);
    item->setObjectName(names.at(index));
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
    QQmlComponent component{&NextAppCore::instance()->engine(), "qrc:/qt/qml/NextAppUi/qml/SplitViewComponent.qml"};
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

    auto h = height();
    if (second_) {
        h = (h /= 2) - split_view_->height();
    }

    first_->setX(0);
    first_->setY(0);
    first_->setWidth(width());
    first_->resetHeight();
    first_->setHeight(h);
    assert(first_->height() == h);

    if (second_) {
        split_view_->setX(0);
        split_view_->setY(h);
        split_view_->setWidth(width());

        second_->setX(0);
        second_->setY(h + split_view_->height());
        second_->setWidth(width());
        second_->setHeight(h);
        assert(second_->height() == h);
    }

    logChildItems("this (after layout)", this);
}
