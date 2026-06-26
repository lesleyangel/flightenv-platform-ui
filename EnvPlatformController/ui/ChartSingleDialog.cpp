#include "ChartSingleDialog.h"
#include <QVBoxLayout>
#include <cmath>
#include <QDebug>

ChartSingleDialog::ChartSingleDialog(const QString& title, QWidget* parent)
    : QWidget(parent) {
    //setWindowTitle(title);
    setMinimumHeight(150);
    
    initChart(title);       // 初始化单图表
}

void ChartSingleDialog::initChart(const QString& title) {
    // 1. 创建单图表
    m_chart = new QChart();
    m_chart->setTitle(title);
    // --- 修改 1: 关闭动画以提升性能 ---
    m_chart->setAnimationOptions(QChart::NoAnimation);
    // --- 修改 2: 隐藏图例以节省空间 (如果不需要的话) ---
    m_chart->legend()->hide(); // 注释掉此行可以保留图例

    // --- 修改 3: 移除图表本身的边距 ---
    // 这是最关键的一步，它会移除图表内容与图表边框之间的空白区域
    m_chart->setMargins(QMargins(0, 0, 0, 0));

    // 2. X轴（数据点序号）
    m_axisX = new QValueAxis();
    m_axisX->setLabelFormat("%d");
    m_axisX->setRange(0, MAX_VISIBLE);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    // 3. Y轴（数据值）
    m_axisY = new QValueAxis();
    m_axisY->setTitleText("数值");
    m_axisY->setRange(0, 100);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // 4. 图表视图（支持交互和抗锯齿）
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setInteractive(true);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    // --- 修改 4: 移除图表视图的边框 ---
    // QChartView 默认有一个边框，需要通过样式表去掉
    m_chartView->setStyleSheet("border: none;");

    // 5. 布局管理
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    // --- 修改 5: 移除布局的边距和间距 ---
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    mainLayout->addWidget(m_chartView);
    setLayout(mainLayout);
}

void ChartSingleDialog::setTitleY(const QString& titley) {
    m_axisY->setTitleText(titley);
}

void ChartSingleDialog::deleteChart() {
    // 删除 series：从 chart 移除并延迟删除（安全）
    while (!m_seriesList.isEmpty()) {
        QLineSeries* series = m_seriesList.takeLast();
        if (m_chart && m_chart->series().contains(series)) {
            m_chart->removeSeries(series);
        }
        if (series) series->deleteLater(); // 安全的延迟删除
    }

    // 移除并删除 axis（axis parent 是 m_chart，所以删除 m_chart 通常会删除 axis）
    if (m_axisX) {
        if (m_chart && m_chart->axes(Qt::Horizontal).contains(m_axisX))
            m_chart->removeAxis(m_axisX);
        m_axisX->deleteLater();
        m_axisX = nullptr;
    }
    if (m_axisY) {
        if (m_chart && m_chart->axes(Qt::Vertical).contains(m_axisY))
            m_chart->removeAxis(m_axisY);
        m_axisY->deleteLater();
        m_axisY = nullptr;
    }

    // 先删除 chartView（它持有显示），再删除 chart（chart 的删除会删除其 child）
    if (m_chartView) {
        QWidget* chartContainer = m_chartView->parentWidget();
        if (chartContainer && chartContainer->layout()) {
            chartContainer->layout()->removeWidget(m_chartView);
        }
        m_chartView->deleteLater();
        m_chartView = nullptr;
    }

    if (m_chart) {
        m_chart->deleteLater();
        m_chart = nullptr;
    }

    // 重置历史 Y 范围等
    m_historyMinY = INFINITY;
    m_historyMaxY = -INFINITY;
    m_totalPoints = 0;
}

void ChartSingleDialog::updateChartXYData(const std::vector<double>& x, const std::vector<double>& y,
    const QString& curveName, Qt::GlobalColor color)
{
    if (!m_chart || !m_axisX || !m_axisY) {
        qCritical() << "图表未初始化";
        return;
    }

    if (x.empty() || y.empty() || x.size() != y.size()) {
        qWarning() << "X/Y数据无效";
        return;
    }

    // 清空旧曲线
    for (auto* s : std::as_const(m_seriesList)) {
        if (m_chart->series().contains(s))
            m_chart->removeSeries(s);
        s->deleteLater();
    }
    m_seriesList.clear();

    QLineSeries* series = new QLineSeries(m_chart);
    series->setName(curveName.isEmpty() ? "曲线" : curveName);
    series->setPen(QPen(color == Qt::transparent ? Qt::red : color, 2));

    bool hasValid = false;
    double minX = INFINITY, maxX = -INFINITY;
    double minY = INFINITY, maxY = -INFINITY;

    for (size_t i = 0; i < x.size(); ++i) {
        double xv = x[i], yv = y[i];
        if (std::isnan(xv) || std::isinf(xv) || std::isnan(yv) || std::isinf(yv))
            continue;
        series->append(xv, yv);
        hasValid = true;
        minX = qMin(minX, xv);
        maxX = qMax(maxX, xv);
        minY = qMin(minY, yv);
        maxY = qMax(maxY, yv);
    }

    if (!hasValid) {
        qWarning() << "无有效XY数据";
        return;
    }

    bool nonMonotonic = false;
    for (size_t i = 1; i < x.size(); ++i) {
        if (x[i] < x[i - 1]) {
            nonMonotonic = true;
            break;
        }
    }

    if (nonMonotonic) {
        series->setPointsVisible(true);
        series->setPointLabelsVisible(false);
        series->setUseOpenGL(true);
    }

    m_chart->addSeries(series);
    series->attachAxis(m_axisX);
    series->attachAxis(m_axisY);
    m_seriesList.append(series);

    // ===== 修正部分：自动调整范围 =====
    double xMargin = (maxX - minX) * 0.1;
    if (xMargin < 1e-9) xMargin = 1.0;
    m_axisX->setRange(minX - xMargin, maxX + xMargin);

    double yMargin = (maxY - minY) * 0.1;
    if (yMargin < 1e-9) yMargin = qMax(1.0, qAbs(maxY) * 0.1);
    m_axisY->setRange(minY - yMargin, maxY + yMargin);

    // ===== 修正部分：强制刷新标题 =====
    if (m_axisY && m_chart->axes().contains(m_axisY)) {
        m_axisY->setTitleText(m_axisY->titleText()); // 如果动态更新，可传入新的titley参数
        m_axisY->applyNiceNumbers();
        m_chart->update();
    }

    qDebug() << "[updateChartXYData] 点数:" << series->count()
        << "X范围:" << minX << maxX
        << "Y范围:" << minY << maxY
        << (nonMonotonic ? "(保持原顺序绘制)" : "(X单调递增)");
}


void ChartSingleDialog::updateChartData(const std::vector<std::vector<double>>& multiData) {
    // 1️⃣ 检查数据有效性
    if (multiData.empty() || multiData[0].empty()) {
        qWarning() << "输入数据为空或格式错误，跳过更新";
        return;
    }

    int curveCount = static_cast<int>(multiData[0].size());

    // 删除多余曲线
    while (m_seriesList.size() > curveCount) {
        QLineSeries* series = m_seriesList.takeLast();
        m_chart->removeSeries(series);
        delete series;
    }

    // 创建缺少的曲线（自动分配颜色）
    QVector<Qt::GlobalColor> colors = {
        Qt::red, Qt::blue, Qt::green, Qt::cyan, Qt::magenta, Qt::yellow,
        Qt::darkRed, Qt::darkBlue, Qt::darkGreen
    };
    while (m_seriesList.size() < curveCount) {
        int idx = m_seriesList.size();
        QLineSeries* newSeries = new QLineSeries();
        newSeries->setName(QString("曲线%1").arg(idx + 1));
        newSeries->setPen(QPen(colors[idx % colors.size()], 2));
        m_chart->addSeries(newSeries);
        newSeries->attachAxis(m_axisX);
        newSeries->attachAxis(m_axisY);
        m_seriesList.append(newSeries);
    }

    // 3️⃣ 更新数据并计算全局Y值范围
    m_totalPoints++;
    double globalMinY = INFINITY;
    double globalMaxY = -INFINITY;
    bool hasValidData = false;

    for (int i = 0; i < curveCount; ++i) {
        QLineSeries* series = m_seriesList[i];
        double yValue = multiData[0][i]; // 取内层第i个数值作为当前点

        if (std::isnan(yValue) || std::isinf(yValue)) continue;

        series->append(m_totalPoints, yValue);
        while (series->count() > MAX_CACHE) {
            series->remove(0);
        }

        globalMinY = qMin(globalMinY, yValue);
        globalMaxY = qMax(globalMaxY, yValue);
        hasValidData = true;
    }

    // 4️⃣ 更新X轴范围
    if (m_totalPoints > MAX_VISIBLE) {
        m_axisX->setRange(m_totalPoints - MAX_VISIBLE, m_totalPoints);
    }
    else {
        m_axisX->setRange(0, MAX_VISIBLE);
    }

    // 5️⃣ 更新Y轴范围
    if (hasValidData) {
        m_historyMinY = qMin(m_historyMinY, globalMinY);
        m_historyMaxY = qMax(m_historyMaxY, globalMaxY);

        //if (m_historyMinY == m_historyMaxY) {
        //    double offset = qMax(1.0, qAbs(m_historyMinY) * 0.1);
        //    m_axisY->setRange(m_historyMinY - offset, m_historyMaxY + offset);
        //}
        //else {
        //    double margin = (m_historyMaxY - m_historyMinY) * 0.1;
        //    margin = qMax(1.0, margin);
        //    m_axisY->setRange(m_historyMinY - margin, m_historyMaxY + margin);
        //}
        if (m_historyMinY == m_historyMaxY) {
            const double margin = qMax(1.0, qAbs(m_historyMinY) * 0.1);
            m_axisY->setRange(m_historyMinY - margin, m_historyMaxY + margin);
        }
        else {
            const double margin = qMax(1.0, (m_historyMaxY - m_historyMinY) * 0.1);
            m_axisY->setRange(m_historyMinY - margin, m_historyMaxY + margin);
        }

        double visibleMinY = INFINITY;
        double visibleMaxY = -INFINITY;
        bool hasVisibleData = false;
        for (const QLineSeries* visibleSeries : m_seriesList) {
            const auto points = visibleSeries->points();
            for (const QPointF& point : points) {
                const double y = point.y();
                if (!std::isfinite(y)) {
                    continue;
                }
                visibleMinY = qMin(visibleMinY, y);
                visibleMaxY = qMax(visibleMaxY, y);
                hasVisibleData = true;
            }
        }
        if (hasVisibleData) {
            m_historyMinY = visibleMinY;
            m_historyMaxY = visibleMaxY;
            if (visibleMinY == visibleMaxY) {
                const double margin = qMax(1.0, qAbs(visibleMinY) * 0.1);
                m_axisY->setRange(visibleMinY - margin, visibleMaxY + margin);
            }
            else {
                const double margin = qMax(1.0, (visibleMaxY - visibleMinY) * 0.1);
                m_axisY->setRange(visibleMinY - margin, visibleMaxY + margin);
            }
        }
    }
    else {
        m_axisY->setRange(0, 100);
        qWarning() << "无有效数据，Y轴使用默认范围";
    }
}
