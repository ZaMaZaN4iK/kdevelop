/* KDevelop CCMake Support
 *
 * Copyright 2012 Aleix Pol Gonzalez <aleixpol@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "cmakebuilderpreferences.h"

#include <QVBoxLayout>

#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>

#include "ui_cmakebuilderpreferences.h"
#include "cmakebuilder.h"
#include "cmakebuilderconfig.h"
#include "cmakeutils.h"

CMakeBuilderPreferences::CMakeBuilderPreferences(KDevelop::IPlugin* plugin, QWidget* parent)
    : KDevelop::ConfigPage(plugin, CMakeBuilderSettings::self(), parent)
{
    QVBoxLayout* l = new QVBoxLayout( this );
    QWidget* w = new QWidget;
    m_prefsUi = new Ui::CMakeBuilderPreferences;
    m_prefsUi->setupUi( w );
    l->addWidget( w );

#ifdef Q_OS_WIN
    m_prefsUi->kcfg_cmakeExe->setFilter("*.exe");
#endif

    m_prefsUi->kcfg_cmakeExe->setToolTip(CMakeBuilderSettings::self()->cmakeExeItem()->whatsThis());
    m_prefsUi->label1->setToolTip(CMakeBuilderSettings::self()->cmakeExeItem()->whatsThis());

    foreach(const QString& generator, CMakeBuilder::supportedGenerators())
        m_prefsUi->kcfg_generator->addItem(generator);
}

CMakeBuilderPreferences::~CMakeBuilderPreferences()
{
    delete m_prefsUi;
}

QString CMakeBuilderPreferences::name() const
{
    return i18n("CMake");
}

QString CMakeBuilderPreferences::fullName() const
{
    return i18n("Configure global CMake settings");
}

QIcon CMakeBuilderPreferences::icon() const
{
    return QIcon::fromTheme("cmake");
}
