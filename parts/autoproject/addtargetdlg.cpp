/***************************************************************************
 *   Copyright (C) 2001 by Bernd Gehrmann                                  *
 *   bernd@kdevelop.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qgroupbox.h>
#include <qlineedit.h>
#include <klocale.h>
#include <kmessagebox.h>

#include "misc.h"
#include "autoprojectwidget.h"
#include "addtargetdlg.h"


AddTargetDialog::AddTargetDialog(AutoProjectWidget *widget, SubprojectItem *item,
                                 QWidget *parent, const char *name)
    : AddTargetDialogBase(parent, name, true)
{
    subProject = item;
    m_widget = widget;

    primary_combo->setFocus();
    primary_combo->insertItem(i18n("Program"));
    primary_combo->insertItem(i18n("Library"));
    primary_combo->insertItem(i18n("Libtool library"));
    primary_combo->insertItem(i18n("Script"));
    primary_combo->insertItem(i18n("Header"));
    primary_combo->insertItem(i18n("Data file"));
    primary_combo->insertItem(i18n("Java"));
    connect( primary_combo, SIGNAL(activated(int)), this, SLOT(primaryChanged()) );

    primaryChanged(); // updates prefix combo

    if (widget->kdeMode())
        ldflagsother_edit->setText("$(all_libraries)");
}


AddTargetDialog::~AddTargetDialog()
{}


void AddTargetDialog::primaryChanged()
{
    QStrList list;
    switch (primary_combo->currentItem()) {
    case 0: // Program
        list.append("bin");
        list.append("sbin");
        list.append("libexec");
        list.append("pkglib");
        list.append("noinst");
        break;
    case 1: // Library
    case 2: // Libtool library
        list.append("lib");
        list.append("pkglib");
        list.append("noinst");
        break;
    case 3: // Script
        list.append("bin");
        list.append("sbin");
        list.append("libexec");
        list.append("pkgdata");
        list.append("noinst");
        break;
    case 4: // Header
        list.append("include");
        list.append("oldinclude");
        list.append("pkginclude");
        list.append("noinst");
        break;
    case 5: // Data
        list.append("bin");
        list.append("sbin");
        list.append("noinst");
        break;
    case 6: // Java
        list.append("java");
        list.append("noinst");
        break;
    }

    prefix_combo->clear();
    
    prefix_combo->insertStrList(list);
    QStrList prefixes;
    QMap<QCString,QCString>::ConstIterator it;
    for (it = subProject->prefixes.begin(); it != subProject->prefixes.end(); ++it)
        prefix_combo->insertItem(QString(it.key()));

    // Only enable ldflags stuff for libtool libraries
    bool lt = primary_combo->currentItem() == 2;
    bool prog = primary_combo->currentItem() == 0;
    allstatic_box->setEnabled(lt);
    avoidversion_box->setEnabled(lt);
    module_box->setEnabled(lt);
    noundefined_box->setEnabled(lt);
    ldflagsother_edit->setEnabled(lt || prog);
}


void AddTargetDialog::accept()
{
    QCString name = filename_edit->text().stripWhiteSpace().latin1();
    QCString prefix = prefix_combo->currentText().latin1();

    QCString primary;
    switch (primary_combo->currentItem()) {
    case 0: primary = "PROGRAMS";    break;
    case 1: primary = "LIBRARIES";   break;
    case 2: primary = "LTLIBRARIES"; break;
    case 3: primary = "SCRIPTS";     break;
    case 4: primary = "HEADERS";     break;
    case 5: primary = "DATA";        break;
    case 6: primary = "JAVA";        break;
    default: ;
    }
    
    if (name.isEmpty()) {
        KMessageBox::sorry(this, i18n("You have to give the target a name"));
        return;
    }

    if (primary == "LTLIBRARIES" && name.right(3) != ".la") {
        KMessageBox::sorry(this, i18n("Libtool libraries must have a .la suffix"));
        return;
    }
    
    QListIterator<TargetItem> it(subProject->targets);
    for (; it.current(); ++it)
        if (name == (*it)->name) {
            KMessageBox::sorry(this, i18n("A target with this name already exists"));
            return;
        }

    QStringList flagslist;
    if (primary == "LTLIBRARIES") {
        if (allstatic_box->isChecked())
            flagslist.append("-all-static");
        if (avoidversion_box->isChecked())
            flagslist.append("-avoid-version");
        if (module_box->isChecked())
            flagslist.append("-module");
        if (noundefined_box->isChecked())
            flagslist.append("-no-undefined");
    }
    flagslist.append(ldflagsother_edit->text());
    QCString ldflags = flagslist.join(" ").latin1();

    TargetItem *titem = m_widget->createTargetItem(name, prefix, primary);
    subProject->targets.append(titem);
    
    QCString canonname = AutoProjectTool::canonicalize(name);
    QCString varname = prefix + "_" + primary;
    subProject->variables[varname] += (QCString(" ") + name);
    
    QMap<QCString,QCString> replaceMap;
    replaceMap.insert(varname, subProject->variables[varname]);
    replaceMap.insert(canonname + "_SOURCES", "");
    if (primary == "LTLIBRARIES" || primary == "PROGRAMS")
        replaceMap.insert(canonname + "_LDFLAGS", ldflags);

    AutoProjectTool::modifyMakefileam(subProject->path + "/Makefile.am", replaceMap);
    
    QDialog::accept();
}

#include "addtargetdlg.moc"
