#include "EnvPredictorUiHelpers.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <sstream>
#include <stdexcept>

namespace {
int g_rootCount = 0;
}

QIcon loadTreeIcon(const QString& iconPath, const QSize& iconSize)
{
    QIcon icon;
    QPixmap pixmap(iconPath);
    if (pixmap.isNull()) {
        qWarning() << "Tree icon load failed! Path:" << iconPath;
        return icon;
    }
    pixmap = pixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.addPixmap(pixmap);
    return icon;
}

void addNewRootNode(QTreeWidget* treeWidget,
    const QString& rootName,
    const QStringList& childNames)
{
    if (!treeWidget) {
        qWarning() << "Invalid QTreeWidget pointer!";
        return;
    }

    if (treeWidget->columnCount() != 1) {
        treeWidget->setColumnCount(1);
        treeWidget->setHeaderHidden(true);
    }

    QIcon rootIcon;
    QPixmap rootPixmap(":/ui/img/align-left.svg");
    if (!rootPixmap.isNull()) {
        rootPixmap = rootPixmap.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        rootIcon.addPixmap(rootPixmap);
    }
    else {
        qWarning() << "Root icon load failed! Path::/ui/img/align-left.svg";
    }

    QIcon childIcon;
    QPixmap childPixmap(":/ui/img/align-left-simple.svg");
    if (!childPixmap.isNull()) {
        childPixmap = childPixmap.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        childIcon.addPixmap(childPixmap);
    }
    else {
        qWarning() << "Child icon load failed! Path::/ui/img/align-left-simple.svg";
    }

    QTreeWidgetItem* rootItem = new QTreeWidgetItem(treeWidget);
    rootItem->setText(0, rootName);
    rootItem->setIcon(0, rootIcon);
    rootItem->setExpanded(true);

    for (const QString& childName : childNames) {
        QTreeWidgetItem* childItem = new QTreeWidgetItem(rootItem);
        childItem->setText(0, childName);
        childItem->setIcon(0, childIcon);
    }

    g_rootCount++;
}

std::vector<int> stringToVector(const std::string& str) {
    std::vector<int> result;
    std::stringstream ss(str);
    char ch;
    int num;

    ss >> ch;

    while (ss >> num) {
        result.push_back(num);
        ss >> ch;
    }

    return result;
}

bool parseIntVectorExact(const std::string& text, size_t expected_size, const char* field_name, std::vector<int>& out)
{
    out = stringToVector(text);
    if (out.size() != expected_size) {
        qWarning() << field_name << "expects" << expected_size
            << "integer values, got" << out.size() << ":"
            << QString::fromStdString(text);
        return false;
    }
    return true;
}

void displayColorSequenceVertically(QWidget* targetWidget,
    const QVector<QColor>& colors,
    const QVector<QString>& colorStrs,
    const QSize& blockSize,
    int spacing,
    bool showHex,
    bool showBorder,
    int borderRadius)
{
    if (!targetWidget) {
        return;
    }

    QList<QWidget*> childWidgets = targetWidget->findChildren<QWidget*>();
    for (QWidget* child : childWidgets) {
        if (child->objectName() == "ColorBlockLabel" || child->objectName() == "ColorTextLabel") {
            child->deleteLater();
        }
    }

    QLayout* oldLayout = targetWidget->layout();
    if (oldLayout) {
        QLayoutItem* item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item;
        }
        delete oldLayout;
    }

    if (colors.isEmpty()) {
        return;
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(targetWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(spacing);
    mainLayout->setAlignment(Qt::AlignTop);

    for (int i = 0; i < colors.size(); ++i) {
        QString textContent = (i < colorStrs.size() && showHex) ? colorStrs[i] : QString("color %1").arg(i + 1);

        QHBoxLayout* itemLayout = new QHBoxLayout();
        itemLayout->setSpacing(6);
        itemLayout->setContentsMargins(0, 0, 0, 0);

        QLabel* colorBlock = new QLabel();
        colorBlock->setObjectName("ColorBlockLabel");
        colorBlock->setFixedSize(blockSize);
        colorBlock->setAlignment(Qt::AlignCenter);

        QString blockStyle = QString("background-color: %1; ").arg(colors[i].name());
        blockStyle += showBorder ? "border: 1px solid #CCCCCC; " : "border: none; ";
        if (borderRadius >= 0) {
            blockStyle += QString("border-radius: %1px; ").arg(borderRadius);
        }
        colorBlock->setStyleSheet(blockStyle);

        QLabel* textLabel = new QLabel();
        textLabel->setObjectName("ColorTextLabel");
        textLabel->setText(textContent);
        textLabel->setStyleSheet("font-size: 12px; color: #333333;");
        textLabel->setAlignment(Qt::AlignVCenter);

        itemLayout->addWidget(colorBlock);
        itemLayout->addWidget(textLabel);
        itemLayout->addStretch(1);
        mainLayout->addLayout(itemLayout);
    }

    mainLayout->addStretch(1);
    targetWidget->setLayout(mainLayout);
    targetWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QVector<QString> getColorLabelTexts(QWidget* targetWidget) {
    QVector<QString> colorTexts;
    if (!targetWidget) {
        return colorTexts;
    }
    QList<QLabel*> textLabels = targetWidget->findChildren<QLabel*>("ColorTextLabel");

    foreach(QLabel * label, textLabels) {
        colorTexts.append(label->text());
    }

    return colorTexts;
}

std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& matrix) {
    if (matrix.empty())
        return {};

    size_t cols = matrix[0].size();
    for (const auto& row : matrix) {
        if (row.size() != cols) {
            throw std::invalid_argument("matrix transpose failed: irregular matrix");
        }
    }

    std::vector<std::vector<double>> transposed(cols, std::vector<double>(matrix.size()));

    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < cols; ++j) {
            transposed[j][i] = matrix[i][j];
        }
    }

    return transposed;
}

void fillTableWidget(QTableWidget* tableWidget, const std::vector<std::vector<QString>>& tableItemContents) {
    if (!tableWidget || tableItemContents.empty()) {
        return;
    }
    const std::vector<QString>& columnNames = tableItemContents[0];
    int columnCount = static_cast<int>(columnNames.size());
    tableWidget->setColumnCount(columnCount);

    for (int col = 0; col < columnCount; ++col) {
        tableWidget->setHorizontalHeaderItem(col, new QTableWidgetItem(columnNames[col]));
    }
    int rowCount = static_cast<int>(tableItemContents.size()) - 1;
    tableWidget->setRowCount(rowCount);

    for (int row = 0; row < rowCount; ++row) {
        const std::vector<QString>& rowData = tableItemContents[row + 1];

        if (rowData.empty()) {
            continue;
        }

        QString rowName = rowData[0];
        tableWidget->setVerticalHeaderItem(row, new QTableWidgetItem(rowName));

        for (int col = 0; col < columnCount; ++col) {
            if (col + 1 < static_cast<int>(rowData.size())) {
                QTableWidgetItem* item = new QTableWidgetItem(rowData[col + 1]);
                tableWidget->setItem(row, col, item);
            }
            else {
                tableWidget->setItem(row, col, new QTableWidgetItem(""));
            }
        }
    }

    tableWidget->resizeColumnsToContents();
}
