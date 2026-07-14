#include "TimelineGraph.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QMetaType>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

struct Bucket {
    QDate start;
    QDate end;
    double hours{};
};

constexpr int labelSpacingPixels = 90;
constexpr int targetBarPixels = 14;
constexpr int maxYTicks = 4;

QString translatedMonthShortName(int month)
{
    static const char* const months[] = {
        QT_TRANSLATE_NOOP("TimelineGraph", "Jan"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Feb"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Mar"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Apr"),
        QT_TRANSLATE_NOOP("TimelineGraph", "May"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Jun"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Jul"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Aug"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Sep"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Oct"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Nov"),
        QT_TRANSLATE_NOOP("TimelineGraph", "Dec")
    };

    if (month < 1 || month > 12) {
        return {};
    }

    return QCoreApplication::translate("TimelineGraph", months[month - 1]);
}

}

TimelineGraph::TimelineGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setImplicitWidth(320);
    setImplicitHeight(220);
}

void TimelineGraph::setPoints(const QVariantList &points)
{
    points_ = points;
    emit pointsChanged();
    update();
}

void TimelineGraph::setSeriesColor(const QColor &color)
{
    if (series_color_ == color) {
        return;
    }

    series_color_ = color;
    emit seriesColorChanged();
    update();
}

void TimelineGraph::setAxisColor(const QColor &color)
{
    if (axis_color_ == color) {
        return;
    }

    axis_color_ = color;
    emit axisColorChanged();
    update();
}

void TimelineGraph::setGridColor(const QColor &color)
{
    if (grid_color_ == color) {
        return;
    }

    grid_color_ = color;
    emit gridColorChanged();
    update();
}

void TimelineGraph::setTextColor(const QColor &color)
{
    if (text_color_ == color) {
        return;
    }

    text_color_ = color;
    emit textColorChanged();
    update();
}

bool TimelineGraph::hasData() const noexcept
{
    return !normalizedPoints().empty();
}

void TimelineGraph::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(boundingRect(), Qt::transparent);

    const auto points = normalizedPoints();
    if (points.empty()) {
        return;
    }

    const auto first_date = points.front().date;
    const auto last_date = points.back().date;
    const auto span_days = std::max<int>(1, static_cast<int>(first_date.daysTo(last_date) + 1));

    QFontMetricsF metrics{painter->font()};
    const qreal left_margin = std::max<qreal>(44.0, metrics.horizontalAdvance(QStringLiteral("88.8 h")) + 8.0);
    const QRectF plot_rect{
        left_margin,
        8.0,
        std::max<qreal>(0.0, width() - left_margin - 12.0),
        std::max<qreal>(0.0, height() - 34.0)
    };
    if (plot_rect.width() <= 0.0 || plot_rect.height() <= 0.0) {
        return;
    }

    double max_hours = 0.0;
    for (const auto& point : points) {
        max_hours = std::max(max_hours, point.hours);
    }

    const auto y_max = std::max(1.0, niceMaximum(max_hours, maxYTicks));
    const auto y_step = y_max / maxYTicks;

    painter->setPen(QPen{grid_color_, 1.0});
    painter->setBrush(Qt::NoBrush);
    for (int tick = 0; tick <= maxYTicks; ++tick) {
        const auto ratio = static_cast<qreal>(tick) / maxYTicks;
        const auto y = plot_rect.bottom() - ratio * plot_rect.height();
        painter->drawLine(QPointF{plot_rect.left(), y}, QPointF{plot_rect.right(), y});

        const auto value = ratio * y_max;
        const QString label = value >= 10.0
            ? QLocale().toString(value, 'f', 0) + QStringLiteral(" h")
            : QLocale().toString(value, 'f', y_step < 1.0 ? 1 : 0) + QStringLiteral(" h");
        painter->setPen(text_color_);
        painter->drawText(QRectF{0.0, y - metrics.height() / 2.0, left_margin - 6.0, metrics.height()},
                          Qt::AlignRight | Qt::AlignVCenter,
                          label);
        painter->setPen(QPen{grid_color_, 1.0});
    }

    painter->setPen(QPen{axis_color_, 1.25});
    painter->drawLine(plot_rect.bottomLeft(), plot_rect.bottomRight());
    painter->drawLine(plot_rect.bottomLeft(), plot_rect.topLeft());

    const bool use_bars = span_days <= 31 && (plot_rect.width() / span_days) >= targetBarPixels;
    std::vector<Bucket> buckets;

    if (use_bars) {
        buckets.reserve(span_days);
        int point_index = 0;
        for (QDate date = first_date; date <= last_date; date = date.addDays(1)) {
            double hours = 0.0;
            if (point_index < static_cast<int>(points.size()) && points[point_index].date == date) {
                hours = points[point_index].hours;
                ++point_index;
            }
            buckets.push_back(Bucket{date, date, hours});
        }
    } else {
        const auto bucket_count = std::clamp(static_cast<int>(plot_rect.width() / 18.0), 2, span_days);
        const auto bucket_span = std::max(1, static_cast<int>(std::ceil(static_cast<double>(span_days) / bucket_count)));
        buckets.reserve(static_cast<size_t>(std::ceil(static_cast<double>(span_days) / bucket_span)));

        int point_index = 0;
        for (QDate bucket_start = first_date; bucket_start <= last_date; bucket_start = bucket_start.addDays(bucket_span)) {
            const auto bucket_end = std::min(last_date, bucket_start.addDays(bucket_span - 1));
            double total_hours = 0.0;
            while (point_index < static_cast<int>(points.size()) && points[point_index].date <= bucket_end) {
                total_hours += points[point_index].hours;
                ++point_index;
            }
            buckets.push_back(Bucket{bucket_start, bucket_end, total_hours});
        }
    }

    if (buckets.empty()) {
        return;
    }

    if (use_bars) {
        const auto slot_width = plot_rect.width() / buckets.size();
        const auto bar_width = std::max<qreal>(2.0, slot_width * 0.72);
        painter->setPen(Qt::NoPen);
        painter->setBrush(series_color_);

        for (int index = 0; index < static_cast<int>(buckets.size()); ++index) {
            const auto ratio = buckets[index].hours / y_max;
            const auto bar_height = ratio * plot_rect.height();
            const auto center_x = plot_rect.left() + (index + 0.5) * slot_width;
            QRectF bar_rect{
                center_x - bar_width / 2.0,
                plot_rect.bottom() - bar_height,
                bar_width,
                std::max<qreal>(bar_height, buckets[index].hours > 0.0 ? 1.5 : 0.0)
            };
            if (bar_rect.height() > 0.0) {
                painter->drawRoundedRect(bar_rect, 2.0, 2.0);
            }
        }
    } else {
        painter->setPen(QPen{series_color_, 2.0});
        painter->setBrush(Qt::NoBrush);

        QPainterPath path;
        for (int index = 0; index < static_cast<int>(buckets.size()); ++index) {
            const auto x = plot_rect.left() + (index + 0.5) * (plot_rect.width() / buckets.size());
            const auto y = plot_rect.bottom() - (buckets[index].hours / y_max) * plot_rect.height();
            if (index == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        painter->drawPath(path);

        painter->setBrush(series_color_);
        for (int index = 0; index < static_cast<int>(buckets.size()); ++index) {
            const auto x = plot_rect.left() + (index + 0.5) * (plot_rect.width() / buckets.size());
            const auto y = plot_rect.bottom() - (buckets[index].hours / y_max) * plot_rect.height();
            painter->drawEllipse(QPointF{x, y}, 2.8, 2.8);
        }
    }

    const auto label_count = buckets.size() == 1
        ? 1
        : std::clamp(static_cast<int>(plot_rect.width() / labelSpacingPixels), 2, static_cast<int>(buckets.size()));
    painter->setPen(text_color_);
    for (int label_index = 0; label_index < label_count; ++label_index) {
        const auto bucket_index = label_count == 1
            ? 0
            : static_cast<int>(std::llround(static_cast<double>(label_index) * (buckets.size() - 1) / (label_count - 1)));
        const auto x = plot_rect.left() + (bucket_index + 0.5) * (plot_rect.width() / buckets.size());
        const auto label = formatAxisLabel(buckets[bucket_index].start, span_days);
        painter->drawText(QRectF{x - 42.0, plot_rect.bottom() + 6.0, 84.0, metrics.height() * 1.2},
                          Qt::AlignHCenter | Qt::AlignTop,
                          label);
    }
}

void TimelineGraph::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QString TimelineGraph::formatAxisLabel(const QDate &date, int spanDays)
{
    if (spanDays <= 120) {
        return QStringLiteral("%1 %2")
            .arg(date.day())
            .arg(translatedMonthShortName(date.month()));
    }

    if (spanDays <= 730) {
        return QStringLiteral("%1 %2")
            .arg(translatedMonthShortName(date.month()))
            .arg(date.year());
    }

    return QString::number(date.year());
}

double TimelineGraph::niceStep(double rawStep)
{
    if (rawStep <= 0.0) {
        return 1.0;
    }

    const auto exponent = std::floor(std::log10(rawStep));
    const auto magnitude = std::pow(10.0, exponent);
    const auto normalized = rawStep / magnitude;

    if (normalized <= 1.0) {
        return magnitude;
    }
    if (normalized <= 2.0) {
        return 2.0 * magnitude;
    }
    if (normalized <= 5.0) {
        return 5.0 * magnitude;
    }
    return 10.0 * magnitude;
}

double TimelineGraph::niceMaximum(double maxValue, int tickCount)
{
    const auto step = niceStep(maxValue / std::max(1, tickCount));
    return std::ceil(maxValue / step) * step;
}

std::vector<TimelineGraph::Point> TimelineGraph::normalizedPoints() const
{
    std::vector<Point> normalized;
    normalized.reserve(points_.size());

    for (const auto& value : points_) {
        const auto map = value.toMap();
        const auto date_value = map.value(QStringLiteral("date"));

        QDate date;
        if (date_value.userType() == QMetaType::QDateTime) {
            date = date_value.toDateTime().date();
        } else if (date_value.userType() == QMetaType::QDate) {
            date = date_value.toDate();
        } else {
            date = QDateTime::fromString(date_value.toString(), Qt::ISODate).date();
        }

        if (!date.isValid()) {
            continue;
        }

        normalized.push_back(Point{
            date,
            std::max(0.0, map.value(QStringLiteral("minutes")).toDouble() / 60.0)
        });
    }

    std::sort(normalized.begin(), normalized.end(), [](const Point& lhs, const Point& rhs) {
        return lhs.date < rhs.date;
    });

    return normalized;
}
