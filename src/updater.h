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

#pragma once

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

class Updater : public QObject
{
    Q_OBJECT

public:
    Updater(QObject *parent = nullptr);

    void checkUpdates(bool manual);

signals:
    void updatesFound();
    void noUpdates();
    void errorOccurred(QString);

private slots:
    void onRequestFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager * const m_manager;
    bool m_isManual = false;
};
