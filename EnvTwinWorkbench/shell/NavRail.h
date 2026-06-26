#pragma once

#include <QHash>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace twin {

class NavRail final : public QWidget {
    Q_OBJECT
public:
    explicit NavRail(QWidget* parent = nullptr);

    void setActive(const QString& pageKey);

signals:
    void pageChanged(const QString& pageKey);

private:
    void addSection(QVBoxLayout* layout, const QString& title);
    void addItem(
        QVBoxLayout* layout,
        const QString& key,
        const QString& label,
        bool enabled,
        const QString& badge = QString());

    QHash<QString, QPushButton*> items_;
    QString active_;
};

} // namespace twin
