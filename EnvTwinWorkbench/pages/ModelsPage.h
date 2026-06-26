#pragma once

#include "../datahub/PdkUiReaders.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace twin {

class KvList;

struct ObjectResourceRow {
    QString groupId;
    QString resourceId;
    QString resourceType;
    QString uri;
    QString componentId;
    QJsonObject rawJson;
};

class ModelsPage final : public QWidget {
    Q_OBJECT
public:
    ModelsPage(
        QString objectPackageRoot,
        QWidget* parent = nullptr);

signals:
    void navigateTo(const QString& page);

private:
    void showDetail(int row);
    void populateTable();

    PdkObjectPackageView objectPackage_;
    QVector<ObjectResourceRow> resources_;
    QComboBox* groupFilter_ = nullptr;
    QComboBox* categoryFilter_ = nullptr;
    QLineEdit* searchBox_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    QLabel* detailApplies_ = nullptr;
    KvList* detailKv_ = nullptr;
};

} // namespace twin
