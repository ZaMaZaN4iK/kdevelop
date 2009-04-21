/*
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "implementationhelperitem.h"
#include <ktexteditor/document.h>
#include <klocalizedstring.h>
#include <language/duchain/duchainutils.h>
#include "helpers.h"
#include <language/duchain/types/functiontype.h>
#include <language/duchain/classfunctiondeclaration.h>
#include <qtfunctiondeclaration.h>
#include <language/codegen/coderepresentation.h>
#include <language/backgroundparser/backgroundparser.h>
#include <interfaces/icore.h>
#include <interfaces/ilanguagecontroller.h>
#include <language/duchain/parsingenvironment.h>
#include <sourcemanipulation.h>
#include <cppduchain.h>

namespace Cpp {

ImplementationHelperItem::ImplementationHelperItem(HelperType type, KDevelop::DeclarationPointer decl, KSharedPtr<Cpp::CodeCompletionContext> context, int _inheritanceDepth, int _listOffset) : NormalDeclarationCompletionItem(decl, KSharedPtr<KDevelop::CodeCompletionContext>::staticCast(context), _inheritanceDepth, _listOffset), m_type(type) {
}

#define RETURN_CACHED_ICON(name) {static QIcon icon(KIcon(name).pixmap(QSize(16, 16))); return icon;}

QString ImplementationHelperItem::getOverrideName() const {
  QString ret;
  if(m_declaration) {
    ret = m_declaration->identifier().toString();
  
    KDevelop::ClassFunctionDeclaration* classDecl = dynamic_cast<KDevelop::ClassFunctionDeclaration*>(declaration().data());
    if(classDecl && completionContext() && completionContext()->duContext()) {
      if(classDecl->isConstructor() || classDecl->isDestructor())
        ret = completionContext()->duContext()->localScopeIdentifier().toString();
      if(classDecl->isDestructor())
        ret = "~" + ret;
    }
  }
  return ret;
}

QVariant ImplementationHelperItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {
  QVariant ret = NormalDeclarationCompletionItem::data(index, role, model);
  if(role == Qt::DecorationRole) {
    if(index.column() == KTextEditor::CodeCompletionModel::Icon) {
      switch(m_type) {
        case Override: {
          KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
          KDevelop::ClassFunctionDeclaration* classFunction = dynamic_cast<ClassFunctionDeclaration*>(m_declaration.data());
          if(classFunction && classFunction->isAbstract())
            RETURN_CACHED_ICON("flag-red");
          
          RETURN_CACHED_ICON("CTparents");
        }
        case CreateDefinition:
          RETURN_CACHED_ICON("CTsuppliers"); ///@todo Better icon?
        case CreateSignalSlot:
          RETURN_CACHED_ICON("dialog-ok-apply"); ///@todo Better icon?
      }
    }
  }
  if(role == Qt::DisplayRole) {
    if(index.column() == KTextEditor::CodeCompletionModel::Prefix) {
      QString prefix;
      if(m_type == Override)
        prefix = i18n("Override");
      if(m_type == CreateDefinition)
        prefix = i18n("Implement");
      if(m_type == CreateSignalSlot)
        return i18n("Create Slot");
      
      ret = prefix + " " + ret.toString();
    }

    if(index.column() == KTextEditor::CodeCompletionModel::Name) {
      KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
      if(m_type == CreateSignalSlot) {
        ret = completionContext()->followingText();
        Cpp::QtFunctionDeclaration* cDecl = dynamic_cast<Cpp::QtFunctionDeclaration*>(m_declaration.data());

        if(cDecl && ret.toString().isEmpty())
          ret = cDecl->identifier().toString();
        
        return ret;
      }else if(m_type == Override) {
        ret = getOverrideName();
      }
      if(declaration().data() && m_type != Override) {
        QualifiedIdentifier parentScope = declaration()->context()->scopeIdentifier(true);
        if(!parentScope.isEmpty())
          ret = parentScope.toString() + "::" + ret.toString();
      }
    }
    if(index.column() == KTextEditor::CodeCompletionModel::Arguments) {
      KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
      KDevelop::ClassFunctionDeclaration* classFunction = dynamic_cast<ClassFunctionDeclaration*>(m_declaration.data());
      if(classFunction && classFunction->isAbstract())
        ret = ret.toString() + " = 0";
    }
  }
  
  if(role == KTextEditor::CodeCompletionModel::ItemSelected) {
    KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
    if(declaration().data() && m_type == Override) {
      QualifiedIdentifier parentScope = declaration()->context()->scopeIdentifier(true);
      return i18n("From %1", parentScope.toString());
    }
  }
  
  if(role == KTextEditor::CodeCompletionModel::InheritanceDepth)
    return QVariant(0);
  return ret;
}

QString ImplementationHelperItem::signaturePart(bool includeDefaultParams) {
  KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
  QString ret;
  createArgumentList(*this, ret, 0, includeDefaultParams, true);
  if(m_declaration->abstractType() && m_declaration->abstractType()->modifiers() & AbstractType::ConstModifier)
    ret += " const";
  return ret;
}

QString ImplementationHelperItem::insertionText(KUrl url, KDevelop::SimpleCursor position, QualifiedIdentifier forceParentScope) {
  KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
  
  QString newText;
  if(!m_declaration)
    return QString();

  DUContext* duContext = 0;
  if(completionContext())
    duContext = completionContext()->duContext(); ///@todo Take the DUContext from somewhere lese
  
  KDevelop::ClassFunctionDeclaration* classFunction = dynamic_cast<ClassFunctionDeclaration*>(m_declaration.data());
  
  ///@todo Move these functionalities into sourcemanipulation.cpp
  if(m_type == Override) {
    if(!useAlternativeText) {
      
      if(!classFunction || !classFunction->isConstructor())
        newText = "virtual ";
      if(m_declaration) {
        FunctionType::Ptr asFunction = m_declaration->type<FunctionType>();
        if(asFunction && asFunction->returnType())
            newText += Cpp::simplifiedTypeString(asFunction->returnType(), duContext) + " ";
        
        newText += getOverrideName();
        
        newText += signaturePart(true);
        newText += ";";
      } else {
        kDebug() << "Declaration disappeared";
        return QString();
      }
    }else{
      newText = alternativeText;
    }
  }else if(m_type == CreateDefinition) {
      QualifiedIdentifier localScope;
      TopDUContext* topContext = DUChainUtils::standardContextForUrl(url);
      if(topContext) {
        DUContext* context = topContext->findContextAt(position);
        if(context)
          localScope = context->scopeIdentifier(true);
      }
      
      QualifiedIdentifier scope = m_declaration->qualifiedIdentifier();
      
      if(!forceParentScope.isEmpty() && !scope.isEmpty()) {
        scope = forceParentScope;
        scope.push(m_declaration->identifier());
      }
      
      if(scope.count() <= localScope.count() || !scope.toString().startsWith(localScope.toString()))
        return QString();
      
      scope = scope.mid( localScope.count() );
      
      FunctionType::Ptr asFunction = m_declaration->type<FunctionType>();
      
      if(asFunction && asFunction->returnType())
          newText += Cpp::simplifiedTypeString(asFunction->returnType(), duContext) + " ";
      newText += scope.toString();
      newText += signaturePart(false);
      
      if(classFunction && classFunction->isConstructor()) {
        KDevelop::DUContext* funCtx = classFunction->internalFunctionContext();
        if(funCtx) {
          int argsGiven = 0;
          bool started = false;
          QVector< KDevelop::DUContext::Import > imports = classFunction->context()->importedParentContexts();
          for(KDevelop::DUContext::Import* it = imports.begin(); it != imports.end() && argsGiven < funCtx->localDeclarations().size(); ++it) {
            KDevelop::DUContext* ctx = it->context(topContext);
            if(ctx && ctx->type() == DUContext::Class && ctx->owner()) {
              Declaration* parentClassDecl = ctx->owner();
              
              if(!started)
                newText += ": ";
              else
                newText += ", ";
              started = true;
              
              newText += parentClassDecl->identifier().toString() + "(";
              int take = funCtx->localDeclarations().size()-argsGiven; ///@todo Allow distributing the arguments among multiple parents in multipe-inheritance case
              for(int a = 0; a < take; ++a) {
                if(a)
                  newText += ", ";
                newText += funCtx->localDeclarations()[argsGiven]->identifier().toString();
                ++argsGiven;
              }
              newText += ")";
            }
          }
        }
      }
      
      newText += "\n{\n";
      
      if(asFunction) {
      
        ClassFunctionDeclaration* overridden = dynamic_cast<ClassFunctionDeclaration*>(DUChainUtils::getOverridden(m_declaration.data()));
        
        if(!forceParentScope.isEmpty())
          overridden = dynamic_cast<ClassFunctionDeclaration*>(m_declaration.data());
        
        if(overridden && !overridden->isAbstract()) {
          if(asFunction->returnType() && asFunction->returnType()->toString() != "void") {
            newText += "return ";
          }
          QualifiedIdentifier baseScope = overridden->qualifiedIdentifier();
          bool foundShorter = true;
          do {
            foundShorter = false;
            QualifiedIdentifier candidate = baseScope;
            if(candidate.count() > 2) {
              candidate.pop();
              QList<Declaration*> decls = m_declaration->context()->findDeclarations(candidate);
              if(decls.contains(overridden)) {
                foundShorter = true;
                baseScope = candidate;
              }
            }
          }while(foundShorter);
          
          newText += baseScope.toString() + "(";
          
          DUContext* ctx = m_declaration->internalContext();
          if(ctx->type() == DUContext::Function) {
            bool first = true;
            foreach(Declaration* decl, ctx->localDeclarations()) {
              if(!first)
                newText += ", ";
              first = false;
              newText += decl->identifier().toString();
            }
          }
          
          newText += ");";
        }
      }
      
      newText += "\n}\n";
  }

  return newText;
}

void ImplementationHelperItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {

  if(m_type == CreateSignalSlot) {
    //Step 1: Decide where to put the declaration
    KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
    
    IndexedString doc;
    {
      QList<DUContext*> containers = completionContext()->memberAccessContainers();
      
      if(containers.isEmpty()) 
        return;
      else
        doc = containers[0]->url();
    }

    lock.unlock();
    //Make sure the top-context is up-to-date, waiting for an update if required
    KDevelop::ReferencedTopDUContext updated( DUChain::self()->waitForUpdate(doc, TopDUContext::AllDeclarationsAndContexts) );
    
    if(!updated) {
      kDebug() << "not creating slot because failed to update" << doc.str();
      return;
    }
    lock.lock();
    
    QList<DUContext*> containers = completionContext()->memberAccessContainers();
    
    if(containers.isEmpty())
      return;
    
    DUContext* classContext = containers.first();
    
    Cpp::SourceCodeInsertion insertion(updated.data());
    insertion.setContext(classContext);
    
    insertion.insertSlot(completionContext()->followingText(), QString::fromUtf8(completionContext()->m_connectedSignalNormalizedSignature));

    QString name = completionContext()->followingText();
    if(name.isEmpty() && m_declaration)
      name = m_declaration->identifier().toString();
    
    lock.unlock();
    
    if(!insertion.changes().applyAllChanges()) {
      kDebug() << "failed";
      return;
    }

    ICore::self()->languageController()->backgroundParser()->addDocument(doc.toUrl());

    QString localText = "SLOT(" + name + "(" + QString::fromUtf8(completionContext()->m_connectedSignalNormalizedSignature) + ")));";
    document->replaceText(word, localText);
  }else{
    document->replaceText(word, insertionText(document->url(), SimpleCursor(word.end())));
  }
}

bool ImplementationHelperItem::dataChangedWithInput() const {
  return m_type == CreateSignalSlot;
}

}
