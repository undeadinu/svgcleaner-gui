/****************************************************************************
**
** SVG Cleaner could help you to clean up your SVG files
** from unnecessary data.
** Copyright (C) 2012-2018 Evgeniy Reizner
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**
****************************************************************************/

#include <QCloseEvent>
#include <QDate>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QtConcurrent/QtConcurrentMap>

#include "settings.h"
#include "aboutdialog.h"
#include "preferences/cleaneroptions.h"
#include "preferences/preferencesdialog.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_model(new TreeModel(this))
    , m_cleaningWatcher(new QFutureWatcher<Task::Output>(this))
#ifdef WITH_CHECK_UPDATES
    , m_updater(new Updater(this))
#endif
{
    ui->setupUi(this);
#ifndef Q_OS_MAC
    ui->btnSelectFolder->setFixedWidth(ui->btnSelectFolder->height() * 1.4);
#endif

    ui->verticalLayout->setContentsMargins(ui->verticalLayout->contentsMargins() * 0.5);

    initWatcher();
    initToolBar();
    initTree();
    updateOutputWidget();

    QRegExp rx("[^\\\\/:?%\"<>\\|\\*]{1,32}");
    ui->lineEditFilePrefix->setValidator(new QRegExpValidator(rx, this));
    ui->lineEditFileSuffix->setValidator(new QRegExpValidator(rx, this));

    new QShortcut(QKeySequence::Open, this, SLOT(onAddFiles()));
    new QShortcut(QKeySequence::Quit, this, SLOT(close()));

    loadSettings();

    ui->progressBar->hide();

#ifdef WITH_CHECK_UPDATES
    connect(m_updater, &Updater::updatesFound, this, &MainWindow::onUpdatesFound);
    connect(m_updater, &Updater::noUpdates, this, &MainWindow::onNoUpdates);
    connect(m_updater, &Updater::errorOccurred, this, &MainWindow::onUpdaterError);
    checkUpdates(false);
#endif
}

MainWindow::~MainWindow()
{
    saveSettings();

    delete ui;
}

static QIcon themeIcon(const QString &name)
{
    const QString fallback = QString(":/breeze/%1.svgz").arg(name);
#ifdef Q_OS_LINUX
    return QIcon::fromTheme(name, QIcon(fallback));
#else
    return QIcon(fallback);
#endif
}

void MainWindow::initToolBar()
{
#ifdef Q_OS_WIN
    // match macOS size
    ui->mainToolBar->setIconSize(QSize(32, 32));
#endif

    ui->actionAddFiles->setIcon(themeIcon("document-new"));
    ui->actionAddFolder->setIcon(themeIcon("folder-new"));
    ui->actionClearTree->setIcon(themeIcon("edit-clear-list"));
    ui->actionStart->setIcon(themeIcon("media-playback-start"));
    ui->actionPause->setIcon(themeIcon("media-playback-pause"));
    ui->actionStop->setIcon(themeIcon("media-playback-stop"));
    ui->actionPreferences->setIcon(themeIcon("preferences-other"));
    ui->actionAbout->setIcon(themeIcon("help-about"));

    ui->actionPause->setVisible(false);
    ui->actionStart->setEnabled(false);
    ui->actionStop->setEnabled(false);

    connect(ui->actionStart, &QAction::triggered, this, &MainWindow::onStart);
    connect(ui->actionPause, &QAction::triggered, this, &MainWindow::onPause);
    connect(ui->actionStop,  &QAction::triggered, this, &MainWindow::onStop);
}

void MainWindow::initTree()
{
    ui->treeView->setModel(m_model);
    ui->treeView->header()->setSectionResizeMode(Column::Name, QHeaderView::Stretch);
    ui->treeView->header()->setSectionResizeMode(Column::SizeBefore, QHeaderView::ResizeToContents);
    ui->treeView->header()->setSectionResizeMode(Column::SizeAfter, QHeaderView::ResizeToContents);
    ui->treeView->header()->setSectionResizeMode(Column::Ratio, QHeaderView::ResizeToContents);
    ui->treeView->header()->setSectionResizeMode(Column::Status, QHeaderView::Fixed);
    ui->treeView->header()->setSectionsMovable(false);

    connect(ui->treeView, &FilesView::fileDropped, [this](const QString &path){
        addFile(path);
        recalcTable();
    });

    connect(ui->treeView, &FilesView::folderDropped, [this](const QString &path){
        addFolder(path);
        recalcTable();
    });

    const QString statusText = m_model->headerData(Column::Status, Qt::Horizontal).toString();
    const int sw = fontMetrics().width(statusText) * 1.4;
    ui->treeView->header()->resizeSection(Column::Status, sw);

    ui->treeView->setItemDelegateForColumn(Column::Status, new StatusDelegate(this));

    connect(ui->treeView, &QTreeView::doubleClicked, this, &MainWindow::onDoubleClick);
}

void MainWindow::initWatcher()
{
    connect(m_cleaningWatcher, &QFutureWatcher<Task::Output>::resultReadyAt,
            this, &MainWindow::onResultReadyAt);
    connect(m_cleaningWatcher, &QFutureWatcher<Task::Output>::finished,
            this, &MainWindow::onFinished);
}

void MainWindow::loadSettings()
{
    AppSettings settings;
    resize(settings.value(SettingKey::WindowSize, QSize(640, 480)).toSize());
    ui->lineEditFolder->setText(settings.string(SettingKey::OutputFolder));

    ui->lineEditFilePrefix->setText(settings.string(SettingKey::FilePrefix));
    ui->lineEditFileSuffix->setText(settings.string(SettingKey::FileSuffix));
}

void MainWindow::saveSettings()
{
    AppSettings settings;
    settings.setValue(SettingKey::WindowSize, size());
    settings.setValue(SettingKey::FilePrefix, ui->lineEditFilePrefix->text());
    settings.setValue(SettingKey::FileSuffix, ui->lineEditFileSuffix->text());
}

#ifdef WITH_CHECK_UPDATES
void MainWindow::checkUpdates(bool manual)
{
    if (!manual) {
        AppSettings settings;
        if (!settings.flag(SettingKey::CheckUpdates)) {
            return;
        }

        const QDate currDate = QDate::currentDate();
        const QDate date = settings.value(SettingKey::LastUpdatesCheck).toDate();
        // check once a day
        if (date == currDate) {
            return;
        }

        settings.setValue(SettingKey::LastUpdatesCheck, currDate);
    }

    m_updater->checkUpdates(manual);
}

void MainWindow::onUpdatesFound()
{
    int ans = QMessageBox::information(this, tr("Update available"),
                                       tr("A new version has been published.\n"
                                          "Proceed to the downloads page?"),
                                       QMessageBox::Yes | QMessageBox::No);
    if (ans == QMessageBox::Yes) {
        // do not download file directly, but open a webpage with releases
        QDesktopServices::openUrl(QUrl("https://github.com/RazrFalcon/svgcleaner-gui/releases"));
    }
}

void MainWindow::onNoUpdates()
{
    QMessageBox::information(this, tr("No updates"),
                             tr("You are using the latest version."));
}

void MainWindow::onUpdaterError(const QString &msg)
{
    QMessageBox::critical(this, tr("Error"),
                          tr("An error occurred during updates checking:\n\n%1.").arg(msg));
}
#endif

void MainWindow::updateOutputWidget()
{
    bool flag1 = AppSettings().integer(SettingKey::SavingMethod) == AppSettings::SelectFolder;
    ui->widgetOutputFolder->setVisible(flag1);

    bool flag2 = AppSettings().integer(SettingKey::SavingMethod) == AppSettings::SameFolder;
    ui->widgetSameFolder->setVisible(flag2);
}

static QString lastPath()
{
    AppSettings settings;
    QString dir = settings.string(SettingKey::LastPath);
    if (!QFileInfo(dir).exists())
        dir = QDir::homePath();
    return dir;
}

void MainWindow::on_actionAddFiles_triggered()
{
    QFileDialog diag(this, tr("Add Files"), lastPath(),
                    tr("SVG Files (*.svg *.svgz)"));
    diag.setFileMode(QFileDialog::ExistingFiles);
    diag.setViewMode(QFileDialog::Detail);
    if (!diag.exec()) {
        return;
    }

    AppSettings().setValue(SettingKey::LastPath, diag.directory().absolutePath());

    bool hasSymlink = false;

    for (const QString &file : diag.selectedFiles()) {
        if (!QFileInfo(file).isSymLink()) {
            addFile(file);
        } else {
            hasSymlink = true;
        }
    }

    if (hasSymlink) {
        QMessageBox::warning(this, tr("Warning"), tr("Symlinks are not supported."));
    }

    recalcTable();
}

void MainWindow::on_actionAddFolder_triggered()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add Folder"), lastPath());
    if (folder.isEmpty()) {
        return;
    }

    if (QFileInfo(folder).isSymLink()) {
        QMessageBox::warning(this, tr("Warning"), tr("Symlinks are not supported."));
        return;
    }

    AppSettings().setValue(SettingKey::LastPath, folder);
    addFolder(folder);
    recalcTable();
}

void MainWindow::onAddFiles()
{
    ui->actionAddFiles->trigger();
}

void MainWindow::on_actionClearTree_triggered()
{
    m_model->clear();
    recalcTable();
}

void MainWindow::setPauseBtnVisible(bool flag)
{
    ui->actionStart->setVisible(!flag);
    ui->actionPause->setVisible(flag);
}

void MainWindow::setEnableGui(bool flag)
{
    ui->actionAddFiles->setEnabled(flag);
    ui->actionAddFolder->setEnabled(flag);
    ui->actionClearTree->setEnabled(flag);
    ui->actionPreferences->setEnabled(flag);
    ui->actionAbout->setEnabled(flag);
    ui->widgetOutputFolder->setEnabled(flag);
    ui->treeView->setReadOnly(!flag);
}

void MainWindow::recalcTable()
{
    m_model->calcFoldersStats();
    ui->actionStart->setEnabled(!m_model->isEmpty());
    ui->treeView->expandAll();
    ui->lblFiles->setText(tr("%1 file(s)").arg(m_model->calcFileCount()));
}

void MainWindow::addFile(const QString &path)
{
    auto res = m_model->addFile(path);
    if (res == TreeModel::AddResult::FileExists) {
        QMessageBox::warning(this, tr("Warning"), tr("File is already in the tree."));
    }
}

void MainWindow::addFolder(const QString &path)
{
    auto res = m_model->addFolder(path);
    if (res == TreeModel::AddResult::FolderExists) {
        QMessageBox::warning(this, tr("Warning"), tr("Folder is already in the tree."));
    } else if (res == TreeModel::AddResult::Empty) {
        QMessageBox::warning(this, tr("Warning"), tr("The folder does not contain any SVG files."));
    }
}

static QString genOutputPath(const QString &outFolder,
                             const QString &rootFolder,
                             const QString &path,
                             AppSettings::SavingMethod method)
{
    QString outPath;
    switch (method) {
        case AppSettings::SelectFolder : {
            outPath += outFolder;
            if (rootFolder.isEmpty()) {
                outPath += QDir::separator();
                outPath += QFileInfo(path).fileName();
            } else {
                outPath += QDir::separator();
                outPath += QDir(rootFolder).dirName();
                outPath += QDir::separator();
                outPath += QDir(rootFolder).relativeFilePath(path);
            }
        } break;
        case AppSettings::SameFolder : {
            AppSettings settings;

            outPath += QFileInfo(path).absolutePath();
            outPath += QDir::separator();
            outPath += settings.string(SettingKey::FilePrefix);
            outPath += QFileInfo(path).completeBaseName();
            outPath += settings.string(SettingKey::FileSuffix);
            outPath += ".svg";
        } break;
        case AppSettings::Overwrite : {
            outPath = path;
        } break;
    }

    // remove SVGZ extension
    if (outPath.endsWith('z') || outPath.endsWith('Z')) {
        outPath.chop(1);
    }

    return outPath;
}

static void genCleanData(TreeItem *root,
                         AppSettings::SavingMethod method,
                         const QString &outFolder,
                         QString &rootFolder,
                         QVector<Task::Config> &data)
{
    for (TreeItem *item : root->childrenList()) {
        if (!item->isEnabled() || item->checkState() != Qt::Checked) {
            continue;
        }

        if (item->isFolder()) {
            if (rootFolder.isEmpty()) {
                rootFolder = item->data().path;
            }
            genCleanData(item, method, outFolder, rootFolder, data);
        } else {
            Task::Config conf;
            conf.inputPath = item->data().path;
            conf.outputPath = genOutputPath(outFolder, rootFolder, item->data().path, method);
            conf.treeItem = item;
            data << conf;
        }
    }
}

static void resetTreeData(TreeModel *m_model, TreeItem *root, bool isOverwriteMode)
{
    for (TreeItem *item : root->childrenList()) {
        if (item->isFolder()) {
            resetTreeData(m_model, item, isOverwriteMode);
        } else {
            if (isOverwriteMode) {
                // update initial file size since it changed
                item->setSizeBefore(QFile(item->data().path).size());
            }

            item->resetCleanerData();
            m_model->itemEditFinished(item);
        }
    }
}

void MainWindow::onStart()
{
    if (m_cleaningWatcher->isPaused()) {
        m_cleaningWatcher->resume();
        setPauseBtnVisible(true);
        return;
    }

    // save a file prefix and suffix
    saveSettings();

    AppSettings settings;

    const auto method = (AppSettings::SavingMethod)settings.integer(SettingKey::SavingMethod);
    const QString outFolder = settings.string(SettingKey::OutputFolder);

    if (method == AppSettings::SelectFolder) {
        if (outFolder.isEmpty() || !QFileInfo(outFolder).isDir() || !QFileInfo(outFolder).exists()) {
            QMessageBox::warning(this, tr("Error"), tr("Invalid output folder."));
            return;
        }
    } else if (method == AppSettings::SameFolder) {
        QString prefix = ui->lineEditFilePrefix->text();
        QString suffix = ui->lineEditFileSuffix->text();

        if (prefix.isEmpty() && suffix.isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("You must set a prefix and/or suffix."));
            return;
        }
    }

    Compressor::Type compressorType = Compressor::None;
    const auto compressionLevel = (Compressor::Level)settings.integer(SettingKey::CompressionLevel);
    const bool compressOnlySvgz = settings.flag(SettingKey::CompressOnlySvgz);
    // check that selected compressor is still exists
    if (settings.flag(SettingKey::UseCompression)) {
        const auto c = Compressor::fromName(settings.string(SettingKey::Compressor));
        compressorType = c.type();
        if (compressorType != Compressor::None && !c.isAvailable()) {
            QMessageBox::warning(this, tr("Error"),
                                  tr("Selected compressor is not found.\n"
                                     "Change it in Preferences."));
            return;
        }
    }

    const QStringList args = CleanerOptions::genArgs();

    resetTreeData(m_model, m_model->rootItem(), method == AppSettings::Overwrite);

    QVector<Task::Config> data;
    QString rootPath;
    genCleanData(m_model->rootItem(), method, outFolder, rootPath, data);

    if (data.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("No files are selected."));
        return;
    }

    {
        // We must check that same folder doesn't have files with the same names,
        // but with different extensions: svg and svgz.
        // The problem is when we unzip SVG we will get two files with the same name and
        // one of them will be overwritten. Which is bad.
        // It can only appear in a multithreaded mode.

        QString duplFile;
        QSet<QString> set;
        for (const auto &t : data) {
            QString path = t.inputPath;
            if (path.endsWith('z') || path.endsWith('Z')) {
                path.chop(1);
            }

            if (set.contains(path)) {
                duplFile = path;
                break;
            }
            set.insert(path);
        }

        if (!duplFile.isEmpty()) {
            QMessageBox::warning(this, tr("Error"),
                tr("You can't have both SVG and SVGZ files with the same name in the one dir.\n\n"
                   "%1\n%2").arg(duplFile, duplFile + "z"));
            return;
        }
    }

    for (Task::Config &conf : data) {
        conf.args = args;
        conf.compressorType = compressorType;
        conf.compressionLevel = compressionLevel;
        conf.compressOnlySvgz = compressOnlySvgz;
    }

    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(data.size());
    ui->progressBar->show();

    setEnableGui(false);
    setPauseBtnVisible(true);
    ui->actionStop->setEnabled(true);

    QThreadPool::globalInstance()->setMaxThreadCount(settings.integer(SettingKey::Jobs));
    m_cleaningWatcher->setFuture(QtConcurrent::mapped(data, &Task::cleanFile));
}

void MainWindow::onPause()
{
    setPauseBtnVisible(false);
    m_cleaningWatcher->pause();
}

void MainWindow::onStop()
{
    ui->actionStop->setEnabled(false);
    ui->progressBar->setMaximum(0); // enable wait animation
    m_cleaningWatcher->cancel();
}

void MainWindow::onResultReadyAt(int idx)
{
    ui->progressBar->setValue(m_cleaningWatcher->progressValue());

    auto res = m_cleaningWatcher->resultAt(idx);

    TreeItem *item = res.item();

    if (res.type() == Status::Error) {
        item->setStatus(Status::Error);
        item->setStatusText(res.errorMsg());
        m_model->itemEditFinished(item);
        return;
    }

    auto d = res.okData();
    item->setStatus(Status::Ok);
    item->setSizeAfter(d.outSize);
    item->setRatio(d.ratio);
    item->setOutputPath(d.outputPath);

    if (res.type() == Status::Ok) {
        item->setStatus(Status::Ok);
    } else if (res.type() == Status::Warning) {
        auto wd = res.warningMsg();
        item->setStatus(Status::Warning);
        item->setStatusText(wd);
    } else {
        Q_UNREACHABLE();
    }

    m_model->itemEditFinished(item);
}

void MainWindow::onFinished()
{
    onStop();
    ui->progressBar->hide();
    m_model->calcFoldersStats();

    // force update, because it's not always invoked automatically
    ui->treeView->resizeColumnToContents(Column::SizeBefore);
    ui->treeView->resizeColumnToContents(Column::SizeAfter);
    ui->treeView->resizeColumnToContents(Column::Ratio);

    setEnableGui(true);
    setPauseBtnVisible(false);
}

void MainWindow::onDoubleClick(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.column() == Column::Status) {
        TreeItem *item = m_model->itemByIndex(index);
        if (item && !item->isFolder() && item->data().status != Status::Ok) {
            QMessageBox::information(this, tr("Status info"), item->data().statusText);
        }
    } else if (index.column() == Column::SizeBefore) {
        TreeItem *item = m_model->itemByIndex(index);
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->data().path));
    } else if (index.column() == Column::SizeAfter) {
        TreeItem *item = m_model->itemByIndex(index);
        if (!item->data().outPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->data().outPath));
        }
    }
}

void MainWindow::on_actionPreferences_triggered()
{
    PreferencesDialog diag(this);
#ifdef WITH_CHECK_UPDATES
    connect(&diag, &PreferencesDialog::checkUpdates, [this](){
        checkUpdates(true);
    });
#endif
    if (diag.exec()) {
        updateOutputWidget();
    }
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog diag(this);
    diag.exec();
}

void MainWindow::on_btnSelectFolder_clicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"),
                               AppSettings().string(SettingKey::OutputFolder));
    if (folder.isEmpty()) {
        return;
    }

    const bool isDirWritable = QFileInfo(QFileInfo(folder).absolutePath()).isWritable();
    if (!isDirWritable) {
        QMessageBox::critical(this, tr("Error"), tr("Selected folder is not writable."));
        return;
    }

    AppSettings().setValue(SettingKey::OutputFolder, folder);
    ui->lineEditFolder->setText(folder);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_cleaningWatcher->isRunning()) {
        auto btn = QMessageBox::question(this, tr("Quit?"),
                                         tr("Cleaning is in progress.\n\n"
                                            "Stop it and quit?"),
                                         QMessageBox::Yes, QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            onStop();
        } else {
            e->ignore();
        }
    }
}
