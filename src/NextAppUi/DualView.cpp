#include <QQmlComponent>
#include <QQmlProperty>

#include "DualView.h"
#include "NextAppCore.h"
#include "logging.h"
#include "util.h"

DualView::DualView(QQuickItem *parent)
    : QQuickItem{parent}
{
    view_paths_.at(static_cast<size_t>(ViewType::Actions)) = "qrc:/qt/qml/NextAppUi/qml/ActionsListView.qml";

    setViews(ViewType::Actions, ViewType::None);
}

void DualView::setViews(DualView::ViewType first, DualView::ViewType second)
{
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
    }

    // TODO: If second_ use SplitView
    QQuickItem *ref = this;
    first_->setParentItem(ref);
    first_->setX(0);
    first_->setY(0);
    first_->setHeight(height());
    first_->setWidth(width());
    first_->setVisible(true);

    LOG_DEBUG << "First view: height=" << first_->height() << " width=" << first_->width();
}

QQuickItem *DualView::createView(ViewType type) {
    QQmlComponent component{&NextAppCore::engine(), view_paths_.at(static_cast<size_t>(type))};

    if (component.isError()) {
        LOG_ERROR << "Failed to create component for" << view_paths_.at(static_cast<size_t>(type));
        return nullptr;
    }

    auto item = qobject_cast<QQuickItem*>(component.create());
    item->setParent(this);
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
