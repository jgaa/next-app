#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include "RuntimeServices.h"

class DualView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickItem* first READ first NOTIFY viewsChanged)
    Q_PROPERTY(QQuickItem* second READ second NOTIFY viewsChanged)
    Q_PROPERTY(qreal splitRatio READ splitRatio WRITE setSplitRatio NOTIFY splitRatioChanged)

public:
    explicit DualView(QQuickItem *parent = nullptr);
    DualView(RuntimeServices& runtime, QQuickItem *parent = nullptr);

    enum ViewType : char {
        Actions,
        Lists,
        WorkSessions,
        //WorkList,
        Calendar,
        //GreenDays,
        None
    };

    Q_ENUM(ViewType)

    /*! Set one or two views to be managed by this DualView.
     * The first view is the main view and the second view is the secondary view.
     * The secondary view is optional.
     */
    Q_INVOKABLE void setViews(ViewType first, ViewType second);
    Q_INVOKABLE void setSplitPosition(qreal y);
    Q_INVOKABLE void adjustSplitBy(qreal deltaY);

    QQuickItem *first() const {
        return first_;
    }

    QQuickItem *second() const {
        return second_;
    }

    qreal splitRatio() const noexcept {
        return split_ratio_;
    }

    void setSplitRatio(qreal ratio);

signals:
    void viewsChanged();
    void splitRatioChanged();

private:
    QQuickItem *createView(ViewType type);
    QQuickItem *getView(ViewType type);
    void init();
    void recalculateLayout();
    void persistSplitRatio() const;

    QQuickItem *split_view_{};
    QQuickItem *first_{};
    QQuickItem *second_{};
    qreal split_ratio_{0.5};
    std::array<QQuickItem *, static_cast<size_t>(ViewType::None)> views_{};
    std::array<QString, static_cast<size_t>(ViewType::None)> view_paths_;
    RuntimeServices& runtime_;
};
