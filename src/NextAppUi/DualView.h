#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>

class DualView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickItem* first READ first NOTIFY viewsChanged)
    Q_PROPERTY(QQuickItem* second READ second NOTIFY viewsChanged)

public:
    explicit DualView(QQuickItem *parent = nullptr);

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

    QQuickItem *first() const {
        return first_;
    }

    QQuickItem *second() const {
        return second_;
    }

signals:
    void viewsChanged();

private:
    QQuickItem *createView(ViewType type);
    QQuickItem *getView(ViewType type);
    void init();
    void recalculateLayout();

    QQuickItem *split_view_{};
    QQuickItem *first_{};
    QQuickItem *second_{};
    std::array<QQuickItem *, static_cast<size_t>(ViewType::None)> views_{};
    std::array<QString, static_cast<size_t>(ViewType::None)> view_paths_;
};
