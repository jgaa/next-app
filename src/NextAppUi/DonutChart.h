#pragma once

#include <QAbstractItemModel>
#include <QColor>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickPaintedItem>

class DonutChart : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged FINAL)
    Q_PROPERTY(qreal holeRatio READ holeRatio WRITE setHoleRatio NOTIFY holeRatioChanged FINAL)
    Q_PROPERTY(bool hasData READ hasData NOTIFY hasDataChanged FINAL)

public:
    explicit DonutChart(QQuickItem *parent = nullptr);

    QAbstractItemModel* model() const noexcept {
        return model_;
    }

    void setModel(QAbstractItemModel *model);

    QColor backgroundColor() const noexcept {
        return background_color_;
    }

    void setBackgroundColor(const QColor& color);

    qreal holeRatio() const noexcept {
        return hole_ratio_;
    }

    void setHoleRatio(qreal ratio);

    bool hasData() const noexcept;

    void paint(QPainter *painter) override;

signals:
    void modelChanged();
    void backgroundColorChanged();
    void holeRatioChanged();
    void hasDataChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void attachModel();
    void detachModel();
    void refresh();
    int colorRole() const;
    int minutesRole() const;
    int summaryRole() const;

    QPointer<QAbstractItemModel> model_;
    QColor background_color_{"transparent"};
    qreal hole_ratio_{0.6};
};
