#include "EnvPredictorUIInternal.h"

using namespace flightenv::platform_ui::internal;

SensorExtraConfigDialog::SensorExtraConfigDialog(QWidget* parent) :QDialog(parent) {
    setWindowTitle(QString::fromUtf8("传感器额外配置"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(QString::fromUtf8("这里放该传感器的高级参数（示例占位）"), this));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this]() { accept(); });
    connect(box, &QDialogButtonBox::rejected, this, [this]() { reject(); });
}



void EnvPredictorUI::buildAcqAndConfigFlat_()
{
    pageAcqConfig_ = new QWidget(this);
    auto* root = new QVBoxLayout(pageAcqConfig_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── 顶部：传感器配置与通道映射 ─────────────────────────────
    {
        auto* grp = new QGroupBox(toQStr("传感器配置与通道映射"), pageAcqConfig_);
        auto* v = new QVBoxLayout(grp);
        auto* tip = new QLabel(toQStr("配置每个通道的字段索引、单位、采样频率、缩放与偏置。"), grp);
        tip->setWordWrap(true); v->addWidget(tip);

        tblSensorMap_ = new QTableWidget(4, 8, grp);

        tblSensorMap_->setHorizontalHeaderLabels({ toQStr("通道"), toQStr("传感器"), toQStr("通道ID"), toQStr("单位"),
            toQStr("采样频率(Hz)"), toQStr("测量噪声"), toQStr("分辨率"), toQStr("状态") });
        tblSensorMap_->horizontalHeader()->setStretchLastSection(true);
        auto mkRow = [&](int r, const QString& name, int idx, const QString& unit, double hz, const QString& sc, double off) {
            tblSensorMap_->setItem(r, 0, new QTableWidgetItem(name));
            auto* cb = new QComboBox(tblSensorMap_);
            cb->addItems({ toQStr("硬件传感器1"), toQStr("虚拟传感器A"), toQStr("数据库源") });
            tblSensorMap_->setCellWidget(r, 1, cb);

            tblSensorMap_->setItem(r, 2, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(idx)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 3, [&]() { QTableWidgetItem* item = new QTableWidgetItem(unit); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 4, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(hz)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 5, [&]() { QTableWidgetItem* item = new QTableWidgetItem(sc); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 6, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(off)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());

            QWidget* checkBoxContainer = new QWidget();
            QHBoxLayout* layout = new QHBoxLayout(checkBoxContainer);
            layout->setSpacing(5);  // 复选框之间的间距
            layout->setContentsMargins(2, 2, 2, 2);  // 容器内边距

            // 创建三个复选框
            QCheckBox* check1 = new QCheckBox();
            QCheckBox* check2 = new QCheckBox();
            QCheckBox* check3 = new QCheckBox();

            QString checkBoxStyle = "QCheckBox {"
                "color: #000000; /* 黑色字体 */"
                "font-size: 12pt; /* 字体大小 */"
                "}"
                "QCheckBox::indicator {"
                "width: 16px;"
                "height: 16px;"
                "}";

            check1->setText(toQStr("连接"));
            check2->setText(toQStr("未连接"));
            check3->setText(toQStr("故障"));

            check1->setStyleSheet(checkBoxStyle);
            check2->setStyleSheet(checkBoxStyle);
            check3->setStyleSheet(checkBoxStyle);

            // 添加到布局
            layout->addWidget(check1, 0, Qt::AlignCenter);
            layout->addWidget(check2, 0, Qt::AlignCenter);
            layout->addWidget(check3, 0, Qt::AlignCenter);
            tblSensorMap_->setCellWidget(r, 7, checkBoxContainer);
            tblSensorMap_->setColumnWidth(7, 120);
            for (size_t i = 0; i < tblSensorMap_->rowCount(); i++)
                tblSensorMap_->setRowHeight(i, 40);
            
            };
        mkRow(0, toQStr("温度 T"), 1, toQStr("°C"), 20.0, "0.0-0.2", 0.5);
        mkRow(1, toQStr("应变 E"), 2, toQStr("με"), 50.0, "0.0-0.2", 0.5);
        mkRow(2, toQStr("热流 Q"), 3, toQStr("W/m^2"), 50.0, "0.0-0.2", 0.5);
        mkRow(3, toQStr("压力 P"), 4, toQStr("Pa"), 50.0, "0.0-0.2", 0.5);
        mkRow(3, toQStr("弹道"), 5, toQStr("-"), 50.0, "0.0-0.2", 0.5);
        v->addWidget(tblSensorMap_);
        root->addWidget(grp);
    }

    // ── 中部：左右并排：系统对齐配置 | 传感器面板 ────────────────
    {
        auto* mid = new QSplitter(Qt::Horizontal, pageAcqConfig_);

        // 左：系统对齐配置
        {
            auto* grp = new QGroupBox(toQStr("系统对齐配置"), mid);
            auto* form = new QFormLayout(grp);
            spBucketMs_ = new QSpinBox(grp); spBucketMs_->setRange(0, 5000); spBucketMs_->setValue(100);
            spLingerMs_ = new QSpinBox(grp); spLingerMs_->setRange(0, 5000); spLingerMs_->setValue(600);
            cbPolicy_ = new QComboBox(grp); cbPolicy_->addItems({ toQStr("等全(AllRequired)"), toQStr("等够(AtLeastM)"), toQStr("到时发(AnyAfterLinger)") });
            spAtLeastM_ = new QSpinBox(grp); spAtLeastM_->setRange(1, 16); spAtLeastM_->setValue(2);
            form->addRow(toQStr("时间桶宽度 bucket_ms"), spBucketMs_);
            form->addRow(toQStr("迟到等待上限 linger_ms"), spLingerMs_);
            form->addRow(toQStr("输出策略"), cbPolicy_);
            form->addRow(toQStr("AtLeastM 的 M"), spAtLeastM_);
            auto* tip = new QLabel(toQStr("说明：以事件时间为主，时间桶削弱抖动；等待上限控制迟到容忍度。"), grp);
            tip->setWordWrap(true); form->addRow(tip);
            mid->addWidget(grp);
        }

        // 右：传感器面板
        {
            auto* grp = new QGroupBox(toQStr("传感器面板（含虚拟/数据库）"), mid);
            auto* lay = new QVBoxLayout(grp);

            auto* topBar = new QHBoxLayout();
            auto* btnMore = new QPushButton(toQStr("更多配置…"), grp);
            connect(btnMore, &QPushButton::clicked, this, [this] { SensorExtraConfigDialog dlg(this); dlg.exec(); });
            topBar->addWidget(btnMore); topBar->addStretch();
            lay->addLayout(topBar);

            auto* hs = new QSplitter(Qt::Horizontal, grp);
            // 左树
            auto* left = new QWidget(hs); auto* vleft = new QVBoxLayout(left);
            treeSensors_ = new QTreeView(left);
            modelSensors_ = new QStandardItemModel(treeSensors_);
            modelSensors_->setHorizontalHeaderLabels({ toQStr("类别"), toQStr("名称/地址") });
            treeSensors_->setModel(modelSensors_);
            treeSensors_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            treeSensors_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
            vleft->addWidget(treeSensors_);
            hs->addWidget(left);
            // 右表
            auto* right = new QWidget(hs); auto* vright = new QVBoxLayout(right);
            lbSensorInfo_ = new QLabel(toQStr("请选择左侧设备查看状态与可配参数"), right);
            tblSensorParams_ = new QTableWidget(0, 2, right);
            tblSensorParams_->setHorizontalHeaderLabels({ toQStr("参数"), toQStr("值") });
            tblSensorParams_->horizontalHeader()->setStretchLastSection(true);
            vright->addWidget(lbSensorInfo_);
            vright->addWidget(tblSensorParams_);
            hs->addWidget(right);

            lay->addWidget(hs);
            mid->addWidget(grp);
        }

        mid->setStretchFactor(1, 1);
        mid->setStretchFactor(2, 2);
        root->addWidget(mid, /*stretch*/1);
    }
    ui.tabWidget_main->insertTab(1, pageAcqConfig_, QString::fromUtf8("采集系统与配置"));
    // 示例填充
    fillMockSensors_();
}

void EnvPredictorUI::fillMockSensors_()
{
    modelSensors_->removeRows(0, modelSensors_->rowCount());
    auto mk = [&](const QString& cat, const QString& name) {
        QList<QStandardItem*> row; row << new QStandardItem(cat) << new QStandardItem(name);
        modelSensors_->appendRow(row);
        };
    mk(toQStr("硬件"), "USB-SERIAL CH340 (COM7)");
    mk(toQStr("硬件"), "Env Gateway v2 (192.168.1.50:5001)");
    mk(toQStr("虚拟"), "VirtualSensor: Sine-Temp(20Hz)");
    mk(toQStr("数据库"), "DBSource: SensorsPacked");
}
