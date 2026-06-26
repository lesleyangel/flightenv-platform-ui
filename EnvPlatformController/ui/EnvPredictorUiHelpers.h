#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QVector>

#include <cstddef>
#include <string>
#include <vector>

class QTableWidget;
class QTreeWidget;
class QWidget;

QIcon loadTreeIcon(const QString& iconPath, const QSize& iconSize = QSize(20, 20));
void addNewRootNode(QTreeWidget* treeWidget, const QString& rootName, const QStringList& childNames);
std::vector<int> stringToVector(const std::string& str);
bool parseIntVectorExact(const std::string& text, size_t expected_size, const char* field_name, std::vector<int>& out);
void displayColorSequenceVertically(
    QWidget* targetWidget,
    const QVector<QColor>& colors,
    const QVector<QString>& colorStrs,
    const QSize& blockSize = QSize(30, 30),
    int spacing = 8,
    bool showHex = true,
    bool showBorder = true,
    int borderRadius = 2);
QVector<QString> getColorLabelTexts(QWidget* targetWidget);
std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& matrix);
void fillTableWidget(QTableWidget* tableWidget, const std::vector<std::vector<QString>>& tableItemContents);
