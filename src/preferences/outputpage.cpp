/****************************************************************************
**
** SVG Cleaner could help you to clean up your SVG files
** from unnecessary data.
** Copyright (C) 2012-2016 Evgeniy Reizner
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

#include "outputpage.h"
#include "ui_outputpage.h"

using namespace CleanerKey;

OutputPage::OutputPage(QWidget *parent) :
    BasePreferencesPage(parent),
    ui(new Ui::OutputPage)
{
    ui->setupUi(this);

    addOptWidgets({
        { ui->chBoxRGB, Output::TrimColors },
        { ui->chBoxSimplifyTransforms, Output::SimplifyTransforms },
        { ui->spinBoxPathsPrecision, Output::PathsPrecision },
    });

    ui->cmbBoxIndent->addItem(tr("None"), "none");
    ui->cmbBoxIndent->addItem(tr("No spaces"), "0");
    ui->cmbBoxIndent->addItem(tr("1 space"), "1");
    ui->cmbBoxIndent->addItem(tr("2 spaces"), "2");
    ui->cmbBoxIndent->addItem(tr("3 spaces"), "3");
    ui->cmbBoxIndent->addItem(tr("4 spaces"), "4");
    ui->cmbBoxIndent->addItem(tr("Tabs"), "tabs");

    loadConfig();
    setupToolTips();
}

OutputPage::~OutputPage()
{
    delete ui;
}

void OutputPage::saveConfig()
{
    BasePreferencesPage::saveConfig();

    CleanerOptions().setValue(CleanerKey::Output::Indent, ui->cmbBoxIndent->currentData());
}

void OutputPage::restoreDefaults()
{
    BasePreferencesPage::restoreDefaults();

    ui->cmbBoxIndent->setCurrentIndex(0);
}

void OutputPage::loadConfig()
{
    BasePreferencesPage::loadConfig();

    int idx = ui->cmbBoxIndent->findData(CleanerOptions().string(CleanerKey::Output::Indent));
    if (idx == -1) {
        idx = 0;
    }

    ui->cmbBoxIndent->setCurrentIndex(idx);
}
