// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pkg_viewer.h"

PKGViewer::PKGViewer(std::shared_ptr<GameInfoClass> game_info_get, QWidget* parent,
                     std::function<void(std::filesystem::path, int, int)> InstallDragDropPkg)
    : QMainWindow(), m_game_info(game_info_get) {
    this->resize(1280, 720);
    this->setAttribute(Qt::WA_DeleteOnClose);
    dir_list_std = Config::getPkgViewer();
    dir_list.clear();
    for (const auto& str : dir_list_std) {
        dir_list.append(QString::fromStdString(str));
    }
    statusBar = new QStatusBar(treeWidget);
    this->setStatusBar(statusBar);
    treeWidget = new QTreeWidget(this);
    treeWidget->setColumnCount(9);
    QStringList headers;
    headers << "Name"
            << "Serial"
            << "Installed"
            << "Size"
            << "Category"
            << "Type"
            << "App Ver"
            << "FW"
            << "Region"
            << "Flags"
            << "Path";
    treeWidget->setHeaderLabels(headers);
    treeWidget->header()->setDefaultAlignment(Qt::AlignCenter);
    treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    treeWidget->setColumnWidth(8, 170);
    this->setCentralWidget(treeWidget);
    QMenuBar* menuBar = new QMenuBar(this);
    menuBar->setContextMenuPolicy(Qt::PreventContextMenu);
    QMenu* fileMenu = menuBar->addMenu(tr("&File"));
    QAction* openFolderAct = new QAction(tr("Open Folder"), this);
    fileMenu->addAction(openFolderAct);
    this->setMenuBar(menuBar);
    CheckPKGFolders(); // Check for new PKG files in existing folders.
    ProcessPKGInfo();

    connect(openFolderAct, &QAction::triggered, this, &PKGViewer::OpenPKGFolder);

    connect(treeWidget, &QTreeWidget::customContextMenuRequested, this,
            [=, this](const QPoint& pos) {
                m_gui_context_menus.RequestGameMenuPKGViewer(pos, m_full_pkg_list, treeWidget,
                                                             InstallDragDropPkg);
            });

    connect(parent, &QWidget::destroyed, this, [this]() { this->deleteLater(); });
}

PKGViewer::~PKGViewer() {}

void PKGViewer::OpenPKGFolder() {
    QString folderPath =
        QFileDialog::getExistingDirectory(this, tr("Open Folder"), QDir::homePath());
    if (!dir_list.contains(folderPath)) {
        dir_list.append(folderPath);
        QDir directory(folderPath);
        QFileInfoList fileInfoList = directory.entryInfoList(QDir::Files);
        for (const QFileInfo& fileInfo : fileInfoList) {
            QString file_ext = fileInfo.suffix();
            if (fileInfo.isFile() && file_ext == "pkg") {
                m_pkg_list.append(fileInfo.absoluteFilePath());
            }
        }
        std::sort(m_pkg_list.begin(), m_pkg_list.end());
        ProcessPKGInfo();
        dir_list_std.clear();
        for (auto dir : dir_list) {
            dir_list_std.push_back(dir.toStdString());
        }
        Config::setPkgViewer(dir_list_std);
    } else {
        // qDebug() << "Folder selection canceled.";
    }
}

void PKGViewer::CheckPKGFolders() { // Check for new PKG file additions.
    m_pkg_list.clear();
    for (const QString& dir : dir_list) {
        QDir directory(dir);
        QFileInfoList fileInfoList = directory.entryInfoList(QDir::Files);
        for (const QFileInfo& fileInfo : fileInfoList) {
            QString file_ext = fileInfo.suffix();
            if (fileInfo.isFile() && file_ext == "pkg") {
                m_pkg_list.append(fileInfo.absoluteFilePath());
            }
        }
    }
    std::sort(m_pkg_list.begin(), m_pkg_list.end());
}

void PKGViewer::ProcessPKGInfo() {
    treeWidget->clear();
    map_strings.clear();
    map_integers.clear();
    m_pkg_app_list.clear();
    m_pkg_patch_list.clear();
    m_full_pkg_list.clear();
    for (int i = 0; i < m_pkg_list.size(); i++) {
        std::filesystem::path path(m_pkg_list[i].toStdString());
#ifdef _WIN32
        path = std::filesystem::path(m_pkg_list[i].toStdWString());
#endif
        package.Open(path);
        psf.Open(package.sfo);
        QString title_name = QString::fromStdString(std::string{*psf.GetString("TITLE")});
        QString title_id = QString::fromStdString(std::string{*psf.GetString("TITLE_ID")});
        QString app_type = game_list_util.GetAppType(*psf.GetInteger("APP_TYPE"));
        QString app_version = QString::fromStdString(std::string{*psf.GetString("APP_VER")});
        QString title_category = QString::fromStdString(std::string{*psf.GetString("CATEGORY")});
        QString pkg_size = game_list_util.FormatSize(package.GetPkgHeader().pkg_size);
        pkg_content_flag = package.GetPkgHeader().pkg_content_flags;
        QString flagss = "";
        for (const auto& flag : package.flagNames) {
            if (package.isFlagSet(pkg_content_flag, flag.first)) {
                if (!flagss.isEmpty())
                    flagss += (", ");
                flagss += QString::fromStdString(flag.second.data());
            }
        }

        u32 fw_int = *psf.GetInteger("SYSTEM_VER");
        QString fw = QString::number(fw_int, 16);
        QString fw_ = fw.length() > 7 ? QString::number(fw_int, 16).left(3).insert(2, '.')
                                      : fw.left(3).insert(1, '.');
        fw_ = (fw_int == 0) ? "0.00" : fw_;
        char region = package.GetPkgHeader().pkg_content_id[0];
        QString pkg_info = "";
        if (title_category == "gd" && !flagss.contains("PATCH")) {
            title_category = "App";
            pkg_info = title_name + ";;" + title_id + ";;" + pkg_size + ";;" + title_category +
                       ";;" + app_type + ";;" + app_version + ";;" + fw_ + ";;" +
                       game_list_util.GetRegion(region) + ";;" + flagss + ";;" + m_pkg_list[i];
            m_pkg_app_list.append(pkg_info);
        } else {
            title_category = "Patch";
            pkg_info = title_name + ";;" + title_id + ";;" + pkg_size + ";;" + title_category +
                       ";;" + app_type + ";;" + app_version + ";;" + fw_ + ";;" +
                       game_list_util.GetRegion(region) + ";;" + flagss + ";;" + m_pkg_list[i];
            m_pkg_patch_list.append(pkg_info);
        }
    }
    std::sort(m_pkg_app_list.begin(), m_pkg_app_list.end());
    for (int i = 0; i < m_pkg_app_list.size(); i++) {
        QTreeWidgetItem* treeItem = new QTreeWidgetItem(treeWidget);
        QStringList pkg_app_ = m_pkg_app_list[i].split(";;");
        m_full_pkg_list.append(m_pkg_app_list[i]);
        treeItem->setExpanded(true);
        treeItem->setText(0, pkg_app_[0]);
        treeItem->setText(1, pkg_app_[1]);
        treeItem->setText(3, pkg_app_[2]);
        treeItem->setTextAlignment(3, Qt::AlignCenter);
        treeItem->setText(4, pkg_app_[3]);
        treeItem->setTextAlignment(4, Qt::AlignCenter);
        treeItem->setText(5, pkg_app_[4]);
        treeItem->setTextAlignment(5, Qt::AlignCenter);
        treeItem->setText(6, pkg_app_[5]);
        treeItem->setTextAlignment(6, Qt::AlignCenter);
        treeItem->setText(7, pkg_app_[6]);
        treeItem->setTextAlignment(7, Qt::AlignCenter);
        treeItem->setText(8, pkg_app_[7]);
        treeItem->setTextAlignment(8, Qt::AlignCenter);
        treeItem->setText(9, pkg_app_[8]);
        treeItem->setText(10, pkg_app_[9]);
        for (const GameInfo& info : m_game_info->m_games) { // Check if game is installed.
            if (info.serial == pkg_app_[1].toStdString()) {
                treeItem->setText(2, QChar(0x2713));
                treeItem->setTextAlignment(2, Qt::AlignCenter);
            }
        }
        for (const QString& item : m_pkg_patch_list) {
            QStringList pkg_patch_ = item.split(";;");
            if (pkg_patch_[1] == pkg_app_[1]) { // check patches with serial.
                m_full_pkg_list.append(item);
                QTreeWidgetItem* childItem = new QTreeWidgetItem(treeItem);
                childItem->setText(0, pkg_patch_[0]);
                childItem->setText(1, pkg_patch_[1]);
                childItem->setText(3, pkg_patch_[2]);
                childItem->setTextAlignment(3, Qt::AlignCenter);
                childItem->setText(4, pkg_patch_[3]);
                childItem->setTextAlignment(4, Qt::AlignCenter);
                childItem->setText(5, pkg_patch_[4]);
                childItem->setTextAlignment(5, Qt::AlignCenter);
                childItem->setText(6, pkg_patch_[5]);
                childItem->setTextAlignment(6, Qt::AlignCenter);
                childItem->setText(7, pkg_patch_[6]);
                childItem->setTextAlignment(7, Qt::AlignCenter);
                childItem->setText(8, pkg_patch_[7]);
                childItem->setTextAlignment(8, Qt::AlignCenter);
                childItem->setText(9, pkg_patch_[8]);
                childItem->setText(10, pkg_patch_[9]);
            }
        }
    }

    for (int column = 0; column < treeWidget->columnCount() - 2; ++column) {
        // Resize the column to fit its contents
        treeWidget->resizeColumnToContents(column);
    }
    // Update status bar.
    statusBar->clearMessage();
    int numPkgs = m_pkg_list.size();
    QString statusMessage = QString::number(numPkgs) + " Package.";
    statusBar->showMessage(statusMessage);
}