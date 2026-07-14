#pragma once

#include <QColor>
#include <QDate>
#include <QQmlEngine>
#include <QQuickPaintedItem>
#include <QVariantList>

class TimelineGraph : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList points READ points WRITE setPoints NOTIFY pointsChanged FINAL)
    Q_PROPERTY(QColor seriesColor READ seriesColor WRITE setSeriesColor NOTIFY seriesColorChanged FINAL)
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY axisColorChanged FINAL)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged FINAL)
    Q_PROPERTY(bool hasData READ hasData NOTIFY pointsChanged FINAL)

public:
    explicit TimelineGraph(QQuickItem *parent = nullptr);

    QVariantList points() const {
        return points_;
    }

    void setPoints(const QVariantList& points);

    QColor seriesColor() const noexcept {
        return series_color_;
    }

    void setSeriesColor(const QColor& color);

    QColor axisColor() const noexcept {
        return axis_color_;
    }

    void setAxisColor(const QColor& color);

    QColor gridColor() const noexcept {
        return grid_color_;
    }

    void setGridColor(const QColor& color);

    QColor textColor() const noexcept {
        return text_color_;
    }

    void setTextColor(const QColor& color);

    bool hasData() const noexcept;

    void paint(QPainter *painter) override;

signals:
    void pointsChanged();
    void seriesColorChanged();
    void axisColorChanged();
    void gridColorChanged();
    void textColorChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    struct Point {
        QDate date;
        double hours{};
    };

    static QString formatAxisLabel(const QDate& date, int spanDays);
    static double niceStep(double rawStep);
    static double niceMaximum(double maxValue, int tickCount);
    std::vector<Point> normalizedPoints() const;

    QVariantList points_;
    QColor series_color_{"#4CAF50"};
    QColor axis_color_{"#808080"};
    QColor grid_color_{"#404040"};
    QColor text_color_{"#C0C0C0"};
};
