/***************************************************************************
                          projectspace.h  -  description
                             -------------------
    begin                : Sat May 13 2000
    copyright            : (C) 2000 by Sandy Meier
    email                : smeier@kdevelop.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PROJECTSPACE_H
#define PROJECTSPACE_H

#include "qobjectplugin.h"
#include "project.h"
#include <qlist.h>
#include <string.h>
#include <ksimpleconfig.h>

/**handles toplevel dir, configure.in, and all projects
    data are stored in NAME.kdevpsp and NAME_local.kdevpsp
  *@author Sandy Meier
  */

class ProjectSpace : public QObjectPlugin  {
  Q_OBJECT
    public: 
  ProjectSpace(QObject* parent=0,const char* name=0);
  ~ProjectSpace();
  
  void addProject(Project* prj);
  void removeProject(QString name);
  
  /** set the projectspace name*/
  void setName(QString name);
  void setAbsolutePath(QString path);
  
  /** Store the name of version control system */
  void setVCSystem(QString vcsystem);
  
  /** stored in the *_local files*/
  void setAuthor(QString name);

  /** set the email, stored in the *_local file */
  void setEmail(QString email);

  /** method to fill up a string template with actual projectspace info
   */
  QString& setInfosInString(QString& strtemplate, bool basics=true);

  /*_____some get methods_____*/
	// member
  /** returns the name of the projectspace*/
  QString getName();

  /** Fetch the name of the version control system */
  QString getVCSystem();

  /** Fetch the authors name. stored in the *_local files*/
  QString getAuthor();

  /** Fetch the authors eMail-address,  stored in the *_local files */
  QString getEmail();

	/***/
	virtual void generateDefaultFiles();
	
  /** writes a NAME.kdevpsp and NAME_local.kdevpsp
      NAME.kdevpsp contains options for all users, like cvs system
      NAME_local contains options from the local user:
  */

  virtual bool readConfig(QString filename);
  virtual bool readGeneralConfig(KSimpleConfig* config);
	virtual bool readUserConfig(KSimpleConfig* config);

	virtual bool writeConfig();	
  virtual bool writeGeneralConfig(KSimpleConfig* config);
	virtual bool writeUserConfig(KSimpleConfig* config);

	
protected:
  /** ProjectSpace name*/
  QString m_name;
  /** the current absolute path to the projectspace */
  QString m_path;
	
  // static
  /** projectspace template, name*/
  QString m_projectspace_template;
  /** Version control object */
//  VersionControl *vc;

  /** all projects in the ProjectSpace*/
  QList<Project>* m_projects;

  /** current active project*/
  Project* m_current_project;
};

#endif
