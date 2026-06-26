#ifndef CHARTSINGLEDIALOG_H
#define CHARTSINGLEDIALOG_H

#include <QDialog>
#include <QtCharts>
#include <vector>
#include <QStringList>

class ChartSingleDialog : public QWidget {
    Q_OBJECT
public:
    explicit ChartSingleDialog(const QString& title, QWidget* parent = nullptr);
    void setTitleY(const QString& titley);
    // 更新数据
    void updateChartData(const std::vector<std::vector<double>>& multiData);
    void deleteChart();
    void updateChartXYData(const std::vector<double>& x, const std::vector<double>& y,
        const QString& curveName = "XY曲线",
        Qt::GlobalColor color = Qt::darkMagenta);
private:
    void initChart(const QString& title); // 初始化图表组件
    
    QChart* m_chart;               // 核心图表（单图表）
    QChartView* m_chartView;       // 图表视图
    QValueAxis* m_axisX;           // X轴（数据点索引）
    QValueAxis* m_axisY;           // Y轴（数据值）
    QList<QLineSeries*> m_seriesList; // 存储多条曲线（同一图表内）

    qint64 m_totalPoints = 0;      // 累计数据点数量（X轴坐标）
    const int MAX_VISIBLE = 50;    // 最多显示50个最新数据点
    const int MAX_CACHE = 50;    // 每条曲线最大缓存数据点

    double m_historyMinY = INFINITY;
    double m_historyMaxY = -INFINITY;
};

#endif // CHARTSINGLEDIALOG_H