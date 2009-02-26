/* This file is part of KDevelop
    Copyright 2006 Hamish Rodda<rodda@kde.org>
    Copyright 2007-2009 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "test_cppcodecompletion.h"

#include <QtTest/QtTest>
#include <typeinfo>

#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/forwarddeclaration.h>
#include <language/duchain/declaration.h>
#include <language/editor/documentrange.h>
#include <language/duchain/classfunctiondeclaration.h>
#include "declarationbuilder.h"
#include "usebuilder.h"
#include "cppeditorintegrator.h"
#include "dumptypes.h"
#include "environmentmanager.h"

#include "tokens.h"
#include "parsesession.h"

#include "rpp/preprocessor.h"
#include "rpp/pp-engine.h"
#include "rpp/pp-environment.h"
#include "expressionvisitor.h"
#include "expressionparser.h"
#include "codecompletion/context.h"
#include "cpppreprocessenvironment.h"
#include "cppduchain/classdeclaration.h"
#include <qstandarditemmodel.h>
#include <language/duchain/functiondefinition.h>

using namespace KTextEditor;

using namespace KDevelop;

QTEST_MAIN(TestCppCodeCompletion)

QString testFile1 = "class Erna; struct Honk { int a,b; enum Enum { Number1, Number2 }; Erna& erna; operator int() {}; }; struct Pointer { Honk* operator ->() const {}; Honk& operator * () {}; }; Honk globalHonk; Honk honky; \n#define HONK Honk\n";

QString testFile2 = "struct Honk { int a,b; enum Enum { Number1, Number2 }; Erna& erna; operator int() {}; }; struct Erna { Erna( const Honk& honk ) {} }; struct Heinz : public Erna {}; Erna globalErna; Heinz globalHeinz; int globalInt; Heinz globalFunction(const Heinz& h ) {}; Erna globalFunction( const Erna& erna); Honk globalFunction( const Honk&, const Heinz& h ) {}; int globalFunction(int ) {}; HONK globalMacroHonk; struct GlobalClass { Heinz function(const Heinz& h ) {}; Erna function( const Erna& erna);  }; GlobalClass globalClass;\n#undef HONK\n";

QString testFile3 = "struct A {}; struct B : public A {};";

QString testFile4 = "void test1() {}; class TestClass() { TestClass() {} };";

QStandardItemModel& fakeModel() {
  static QStandardItemModel model;
  model.setColumnCount(10);
  model.setRowCount(10);
  return model;
}

namespace QTest {
  template<>
  char* toString(const Cursor& cursor)
  {
    QByteArray ba = "Cursor(";
    ba += QByteArray::number(cursor.line()) + ", " + QByteArray::number(cursor.column());
    ba += ')';
    return qstrdup(ba.data());
  }
  template<>
  char* toString(const QualifiedIdentifier& id)
  {
    QByteArray arr = id.toString().toLatin1();
    return qstrdup(arr.data());
  }
  template<>
  char* toString(const Identifier& id)
  {
    QByteArray arr = id.toString().toLatin1();
    return qstrdup(arr.data());
  }
  /*template<>
  char* toString(QualifiedIdentifier::MatchTypes t)
  {
    QString ret;
    switch (t) {
      case QualifiedIdentifier::NoMatch:
        ret = "No Match";
        break;
      case QualifiedIdentifier::Contains:
        ret = "Contains";
        break;
      case QualifiedIdentifier::ContainedBy:
        ret = "Contained By";
        break;
      case QualifiedIdentifier::ExactMatch:
        ret = "Exact Match";
        break;
    }
    QByteArray arr = ret.toString().toLatin1();
    return qstrdup(arr.data());
  }*/
  template<>
  char* toString(const Declaration& def)
  {
    QString s = QString("Declaration %1 (%2): %3").arg(def.identifier().toString()).arg(def.qualifiedIdentifier().toString()).arg(reinterpret_cast<long>(&def));
    return qstrdup(s.toLatin1().constData());
  }
  template<>
  char* toString(const TypePtr<AbstractType>& type)
  {
    QString s = QString("Type: %1").arg(type ? type->toString() : QString("<null>"));
    return qstrdup(s.toLatin1().constData());
  }
}

//Helper-class for testing completion-items
//Just initialize it with the context and the text, and then use the members, for simple cases only "names"
struct CompletionItemTester {
  CompletionItemTester(DUContext* context, QString text = "; ") {
    completionContext = new  Cpp::CodeCompletionContext(DUContextPointer(context), text, QString());
    bool abort = false;
    items = completionContext->completionItems(context->range().end, abort);
    foreach(Item i, items)
      names << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
  }
  
  QStringList names; //Names of all completion-items, not sorted
  typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
  QList <Item > items; //All items retrieved, sorted by name
  
  Cpp::CodeCompletionContext::Ptr completionContext;
  
  //Convenience-function to retrieve data from completion-items by name
  QVariant itemData(QString itemName, int column = KTextEditor::CodeCompletionModel::Name, int role = Qt::DisplayRole) {
    return itemData(names.indexOf(itemName), column, role);
  }
  
  QVariant itemData(int itemNumber, int column = KTextEditor::CodeCompletionModel::Name, int role = Qt::DisplayRole) {
    if(itemNumber < 0 || itemNumber >= items.size())
      return QVariant();
    
    return items[itemNumber]->data(fakeModel().index(0, column), role, 0);
  }
};

#define TEST_FILE_PARSE_ONLY if (testFileParseOnly) QSKIP("Skip", SkipSingle);
TestCppCodeCompletion::TestCppCodeCompletion()
{
  testFileParseOnly = false;
}

void TestCppCodeCompletion::initTestCase()
{
  DUChain::self()->disablePersistentStorage();
  typeInt = AbstractType::Ptr(new IntegralType(IntegralType::TypeInt));

  addInclude( "testFile1.h", testFile1 );
  addInclude( "testFile2.h", testFile2 );
  addInclude( "testFile3.h", testFile3 );
}

void TestCppCodeCompletion::cleanupTestCase()
{
}

Declaration* TestCppCodeCompletion::findDeclaration(DUContext* context, const Identifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

Declaration* TestCppCodeCompletion::findDeclaration(DUContext* context, const QualifiedIdentifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

void TestCppCodeCompletion::testPrivateVariableCompletion() {
  TEST_FILE_PARSE_ONLY
  QByteArray test = "class C {void test() {}; int i; };";

  DUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(context->childContexts().count(), 1);
  DUContext* CContext = context->childContexts()[0];
  QCOMPARE(CContext->type(), DUContext::Class);
  QCOMPARE(CContext->childContexts().count(), 2);
  QCOMPARE(CContext->localDeclarations().count(), 2);
  DUContext* testContext = CContext->childContexts()[1];
  QCOMPARE(testContext->type(), DUContext::Other );
  QVERIFY(testContext->owner());
  QCOMPARE(testContext->localScopeIdentifier(), QualifiedIdentifier("test"));
  lock.unlock();
  Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(testContext), "; ", QString()) );
  bool abort = false;
  typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
  
  QList <Item > items = cptr->completionItems(context->range().end, abort);
  
  bool hadThis = false;
  
  foreach(Item i, items) {
    Cpp::NormalDeclarationCompletionItem* decItem  = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(i.data());
    kDebug() << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
    if(decItem) {
      kDebug() << typeid(*i.data()).name();
      QVERIFY(decItem);
      kDebug() << decItem->declaration()->toString();
    }else{
      Cpp::TypeConversionCompletionItem* conversion = dynamic_cast<Cpp::TypeConversionCompletionItem*>(i.data());
      QVERIFY(conversion);
      QCOMPARE(conversion->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString(), QString("this"));
      hadThis = true;
    }
  }
  //"this" is not found, because it's part of the keyword-completion now
//   QVERIFY(hadThis);
  QCOMPARE(items.count(), 3); //C, test, i, and "this"

  lock.lock();
  release(context);
}

void TestCppCodeCompletion::testCompletionPrefix() {
  TEST_FILE_PARSE_ONLY
  {
    QByteArray method("struct Test {int m;}; Test t;Test* t2;void test() {}");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().size(), 3);
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "if((t).").names, QStringList() << "m");
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test t(&t2->").names, QStringList() << "m");

    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test(\"(\").").names, QStringList() << "m");
    
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test(\" \\\" quotedText( \\\" \").").names, QStringList() << "m");
    
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext());
    bool abort = false;
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext()->completionItems(top->range().end, abort).size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext()->completionItems(top->range().end, abort).size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext()->completionItems(top->range().end, abort)[0]->typeForArgumentMatching().size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext()->completionItems(top->range().end, abort)[0]->typeForArgumentMatching().size());
    
    release(top);
  }
}

void TestCppCodeCompletion::testInheritanceVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("class A { public: class AMyClass {}; }; class B : protected A { public: class BMyClass {}; }; class C : private B{ public: class CMyClass {}; }; class D : public C { class DMyClass{}; }; ");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 4);

  QCOMPARE(top->childContexts()[1]->type(), DUContext::Class);
  QVERIFY(top->childContexts()[1]->owner());
  QVERIFY(Cpp::localClassFromCodeContext(top->childContexts()[1]));
  //From within B, MyClass is visible, because of the protected inheritance
  QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 1);
  QVERIFY(!Cpp::isAccessible(top, dynamic_cast<ClassMemberDeclaration*>(top->childContexts()[0]->localDeclarations()[0]), top, top->childContexts()[1]));
  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names, QStringList() << "AMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[1]).names.toSet(), QSet<QString>() << "BMyClass" << "AMyClass" << "A" << "B" );
  QCOMPARE(CompletionItemTester(top, "A::").names, QStringList() << "AMyClass");
  kDebug() << "list:" << CompletionItemTester(top, "B::").names << CompletionItemTester(top, "A::").names.size();
  QCOMPARE(CompletionItemTester(top, "B::").names, QStringList() << "BMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[2]).names.toSet(), QSet<QString>() << "CMyClass" << "BMyClass" << "AMyClass" << "C" << "B" << "A");
  QCOMPARE(CompletionItemTester(top, "C::").names.toSet(), QSet<QString>() << "CMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[3]).names.toSet(), QSet<QString>() << "DMyClass" << "CMyClass" << "D" << "C" << "B" << "A");
  QCOMPARE(CompletionItemTester(top, "D::").names.toSet(), QSet<QString>() << "CMyClass" ); //DMyClass is private
}



void TestCppCodeCompletion::testConstVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("struct test { void f(); void e() const; }; int main() { const test a; } void test::e() const { }");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 5);

  kDebug() << "list:" << CompletionItemTester(top->childContexts()[2], "a.").names << CompletionItemTester(top->childContexts()[2], "a.").names.size();
  QCOMPARE(CompletionItemTester(top->childContexts()[2], "a.").names.toSet(), QSet<QString>() << "e");
  kDebug() << "list:" << CompletionItemTester(top->childContexts()[4], "").names << CompletionItemTester(top->childContexts()[4], "").names.size();
  QCOMPARE(CompletionItemTester(top->childContexts()[4], "").names.toSet(), QSet<QString>() << "e" << "test" << "main");
}

void TestCppCodeCompletion::testFriendVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("class A { class PrivateClass {}; friend class B; }; class B{};");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 2);

  //No type within A, so there should be no items
  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names, QStringList() << "PrivateClass");
}

void TestCppCodeCompletion::testLocalUsingNamespace() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("namespace Foo { int test() {} } void Bar() { using namespace Foo; int b = test(); }");
  TopDUContext* top = parse(method, DumpAll);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[0]->localDeclarations().size(), 1);
  QCOMPARE(top->childContexts()[2]->localDeclarations().size(), 2);
  QVERIFY(top->childContexts()[0]->localDeclarations()[0]->uses().size());
  QVERIFY(top->childContexts()[2]->findLocalDeclarations(KDevelop::globalImportIdentifier, KDevelop::SimpleCursor::invalid(), 0, KDevelop::AbstractType::Ptr(), KDevelop::DUContext::NoFiltering).size());
//   QVERIFY(top->childContexts()[2]->findDeclarations(KDevelop::globalImportIdentifier).size());
  
  QVERIFY(CompletionItemTester(top->childContexts()[2]).names.contains("test"));
  release(top);
}

void TestCppCodeCompletion::testTemplateMemberAccess() {
  {
    QByteArray method("template<class T> struct I; template<class T> class Test { public: typedef I<T> It; }; template<class T> struct I { }; Test<int>::It test;");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 4);
    AbstractType::Ptr type = top->localDeclarations()[3]->abstractType();
    QVERIFY(type);
    IdentifiedType* identified = dynamic_cast<IdentifiedType*>(type.unsafeData());
    QVERIFY(identified);
    QVERIFY(!identified->declarationId().isDirect());
    QString specializationString = IndexedInstantiationInformation(identified->declarationId().specialization()).information().toString();
    kDebug() << "specialization:" << identified->declarationId().specialization() << specializationString;
    QCOMPARE(specializationString, QString("<int>"));
    QCOMPARE(top->localDeclarations()[3]->abstractType()->toString().remove(' '), QString("I<int>"));
    
    lock.unlock();
    parse(method, DumpNone, 0, KUrl(), top);
    lock.lock();

    QCOMPARE(top->localDeclarations().count(), 4);
    QVERIFY(top->localDeclarations()[3]->abstractType());
    QCOMPARE(top->localDeclarations()[3]->abstractType()->toString().remove(' '), QString("I<int>"));
    
    release(top);
  }
  {
    QByteArray method("template<class T> class Test { public: T member; typedef T Data; enum { Value = 3 }; }; typedef Test<int> IntTest; void test() { IntTest tv; int i = Test<int>::Value; }");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "Test<int>::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");

    lock.unlock();
    parse(method, DumpNone, 0, KUrl(), top);
    lock.lock();

    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->type(), DUContext::Other);
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "IntTest::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "Test<int>::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "tv.").names.toSet(), QSet<QString>() << "member");
    release(top);
  }
}

void TestCppCodeCompletion::testNamespaceCompletion() {
  
  QByteArray method("namespace A { class m; namespace Q {}; }; namespace A { class n; int q; }");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("A"));
  QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("A"));
  QCOMPARE(top->localDeclarations()[0]->kind(), Declaration::Namespace);
  QCOMPARE(top->localDeclarations()[1]->kind(), Declaration::Namespace);
  QVERIFY(!top->localDeclarations()[0]->abstractType());
  QVERIFY(!top->localDeclarations()[1]->abstractType());
  QCOMPARE(top->localDeclarations()[0]->internalContext(), top->childContexts()[0]);
  QCOMPARE(top->localDeclarations()[1]->internalContext(), top->childContexts()[1]);
  
  QCOMPARE(CompletionItemTester(top).names, QStringList() << "A");

  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names.toSet(), QSet<QString>() << "m" << "n" << "Q");
  QCOMPARE(CompletionItemTester(top).itemData("A", KTextEditor::CodeCompletionModel::Prefix).toString(), QString("namespace"));
  release(top);
}

void TestCppCodeCompletion::testSameNamespace() {

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace A { class C { }; void test2(); } namespace A { void test() { } }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  FunctionDefinition* funDef = dynamic_cast<KDevelop::FunctionDefinition*>(top->childContexts()[1]->localDeclarations()[0]);
  QVERIFY(!funDef->hasDeclaration());

//   lock.unlock();
  {
    kDebug() << CompletionItemTester(top->childContexts()[1]->childContexts()[1]).names;
    QCOMPARE(CompletionItemTester(top->childContexts()[1]->childContexts()[1]).names.toSet(), QSet<QString>() << "C" << "test2" << "test" << "A");
  }
  
  release(top);
}

void TestCppCodeCompletion::testUnnamedNamespace() {
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace {int a;} namespace { int b; }; void test() {}");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 4);
  QVERIFY(findDeclaration(top, QualifiedIdentifier("a")));
  QVERIFY(findDeclaration(top, QualifiedIdentifier("b")));

//   lock.unlock();
  {
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(top), "; ", QString()) );
    bool abort = false;
    typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
    
    QList <Item > items = cptr->completionItems(top->range().end, abort);
    foreach(Item i, items) {
      Cpp::NormalDeclarationCompletionItem* decItem  = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(i.data());
      QVERIFY(decItem);
      kDebug() << decItem->declaration()->toString();
      kDebug() << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
    }
    
    //Have been filtered out, because only types are shown from the global scope
    QCOMPARE(items.count(), 0); //C, test, and i
  }
  {
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(top->childContexts()[3]), "; ", QString()) );
    bool abort = false;
    typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
    
    QList <Item > items = cptr->completionItems(top->range().end, abort);
    foreach(Item i, items) {
      Cpp::NormalDeclarationCompletionItem* decItem  = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(i.data());
      QVERIFY(decItem);
      kDebug() << decItem->declaration()->toString();
      kDebug() << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
    }
    
    QCOMPARE(items.count(), 3); //b, a, and test
  }
  
//   lock.lock();
  release(top);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestCppCodeCompletion::testCompletionContext() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "#include \"testFile1.h\"\n";
  test += "#include \"testFile2.h\"\n";
  test += "void test() { }";

  DUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(context->childContexts().count());
  DUContext* testContext = context->childContexts()[0];
  QCOMPARE( testContext->type(), DUContext::Function );

  lock.unlock();
  {
    ///Test whether a recursive function-call context is created correctly
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(DUContextPointer(context)), "; globalFunction(globalFunction(globalHonk, ", QString() ) );
    Cpp::CodeCompletionContext& c(*cptr);
    QVERIFY( c.isValid() );
    QVERIFY( c.memberAccessOperation() == Cpp::CodeCompletionContext::NoMemberAccess );
    QVERIFY( !c.memberAccessContainer().isValid() );

    //globalHonk is of type Honk. Check whether all matching functions were rated correctly by the overload-resolution.
    //The preferred parent-function in the list should be "Honk globalFunction( const Honk&, const Heinz& h )", because the first argument maches globalHonk
    Cpp::CodeCompletionContext* function = c.parentContext();
    QVERIFY(function);
    QVERIFY(function->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess);
    QVERIFY(!function->functions().isEmpty());

    lock.lock();
    for( Cpp::CodeCompletionContext::FunctionList::const_iterator it = function->functions().begin(); it != function->functions().end(); ++it )
      kDebug(9007) << (*it).function.declaration()->toString() << ((*it).function.isViable() ? QString("(viable)") : QString("(not viable)")) ;
    lock.unlock();

    QCOMPARE(function->functions().size(), 4);
    QVERIFY(function->functions()[0].function.isViable());
    //Because Honk has a conversion-function to int, globalFunction(int) is yet viable(although it can take only 1 parameter)
    QVERIFY(function->functions()[1].function.isViable());
    //Because Erna has a constructor that takes "const Honk&", globalFunction(Erna) is yet viable(although it can take only 1 parameter)
    QVERIFY(function->functions()[2].function.isViable());
    //Because a value of type Honk is given, 2 globalFunction's are not viable
    QVERIFY(!function->functions()[3].function.isViable());


    function = function->parentContext();
    QVERIFY(function);
    QVERIFY(function->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess);
    QVERIFY(!function->functions().isEmpty());
    QVERIFY(!function->parentContext());
    QVERIFY(function->functions().size() == 4);
    //Because no arguments were given, all functions are viable
    QVERIFY(function->functions()[0].function.isViable());
    QVERIFY(function->functions()[1].function.isViable());
    QVERIFY(function->functions()[2].function.isViable());
    QVERIFY(function->functions()[3].function.isViable());
  }

  {
    ///The context is a function, and there is no prefix-expression, so it should be normal completion.
    DUContextPointer contPtr(context);
    Cpp::CodeCompletionContext c(contPtr, "{", QString() );
    QVERIFY( c.isValid() );
    QVERIFY( c.memberAccessOperation() == Cpp::CodeCompletionContext::NoMemberAccess );
    QVERIFY( !c.memberAccessContainer().isValid() );
  }

  lock.lock();
  release(context);
}


void TestCppCodeCompletion::testTypeConversion() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "#include \"testFile1.h\"\n";
  test += "#include \"testFile2.h\"\n";
  test += "#include \"testFile3.h\"\n";
  test += "int n;\n";
  test += "void test() { }\n";

  DUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  DUContext* testContext = context->childContexts()[0];
  QCOMPARE( testContext->type(), DUContext::Function );

  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Heinz") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Erna") ));
  Declaration* decl = findDeclaration( testContext, QualifiedIdentifier("Erna") );
  QVERIFY(decl);
  QVERIFY(decl->logicalInternalContext(context->topContext()));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Honk") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("A") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("B") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("test") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("n") ));
  AbstractType::Ptr Heinz = findDeclaration( testContext, QualifiedIdentifier("Heinz") )->abstractType();
  AbstractType::Ptr Erna = findDeclaration( testContext, QualifiedIdentifier("Erna") )->abstractType();
  AbstractType::Ptr Honk = findDeclaration( testContext, QualifiedIdentifier("Honk") )->abstractType();
  AbstractType::Ptr A = findDeclaration( testContext, QualifiedIdentifier("A") )->abstractType();
  AbstractType::Ptr B = findDeclaration( testContext, QualifiedIdentifier("B") )->abstractType();
  AbstractType::Ptr n = findDeclaration( testContext, QualifiedIdentifier("n") )->abstractType();

  QVERIFY(n);

  {
    FunctionType::Ptr test = findDeclaration( testContext, QualifiedIdentifier("test") )->type<FunctionType>();
    QVERIFY(test);

    Cpp::TypeConversion conv(context->topContext());
    QVERIFY(!conv.implicitConversion(test->returnType()->indexed(), Heinz->indexed(), false));
    QVERIFY(!conv.implicitConversion(Heinz->indexed(), test->returnType()->indexed(), false));
    QVERIFY(!conv.implicitConversion(test->returnType()->indexed(), n->indexed(), false));
    QVERIFY(!conv.implicitConversion(n->indexed(), test->returnType()->indexed(), false));
  }
  //lock.unlock();
  {
    ///Test whether a recursive function-call context is created correctly
    Cpp::TypeConversion conv(context->topContext());
    QVERIFY( !conv.implicitConversion(Honk->indexed(), Heinz->indexed()) );
    QVERIFY( conv.implicitConversion(Honk->indexed(), typeInt->indexed()) ); //Honk has operator int()
    QVERIFY( conv.implicitConversion(Honk->indexed(), Erna->indexed()) ); //Erna has constructor that takes Honk
    QVERIFY( !conv.implicitConversion(Erna->indexed(), Heinz->indexed()) );

    ///@todo reenable once base-classes are parsed correctly
    //QVERIFY( conv.implicitConversion(B, A) ); //B is based on A, so there is an implicit copy-constructor that creates A from B
    //QVERIFY( conv.implicitConversion(Heinz, Erna) ); //Heinz is based on Erna, so there is an implicit copy-constructor that creates Erna from Heinz

  }

  //lock.lock();
  release(context);
}

KDevelop::IndexedType toReference(IndexedType t) {
  
  ReferenceType::Ptr refType( new ReferenceType);
  refType->setBaseType(t.type());
  return refType->indexed();
}

KDevelop::IndexedType toPointer(IndexedType t) {
  
  PointerType::Ptr refType( new PointerType);
  refType->setBaseType(t.type());
  return refType->indexed();
}

void TestCppCodeCompletion::testTypeConversion2() {
  {
    QByteArray test = "class A {}; class B {public: explicit B(const A&) {}; private: operator A() const {}; }; class C : public B{private: C(B) {}; };";
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 3);
    Cpp::TypeConversion conv(context);
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[1]->indexedType()) );

    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[2]->indexedType()), toReference(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( conv.implicitConversion(toReference(context->localDeclarations()[2]->indexedType()), toReference(context->localDeclarations()[1]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[1]->indexedType()), toReference(context->localDeclarations()[2]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[1]->indexedType()), toReference(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[0]->indexedType()), toReference(context->localDeclarations()[1]->indexedType()) ));

    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[2]->indexedType()), toPointer(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( conv.implicitConversion(toPointer(context->localDeclarations()[2]->indexedType()), toPointer(context->localDeclarations()[1]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[1]->indexedType()), toPointer(context->localDeclarations()[2]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[1]->indexedType()), toPointer(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[0]->indexedType()), toPointer(context->localDeclarations()[1]->indexedType()) ));
    
    release(context);
  }
  {
    QByteArray test = "const char** b; char** c; char** const d; char* const * e; char f; const char q; ";
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 6);
    Cpp::TypeConversion conv(context);
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    PointerType::Ptr fromPointer = context->localDeclarations()[1]->indexedType().type().cast< PointerType>();
    QVERIFY(fromPointer);
    QVERIFY( !(fromPointer->modifiers() & AbstractType::ConstModifier));
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[3]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[3]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[4]->indexedType(), context->localDeclarations()[5]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[5]->indexedType(), context->localDeclarations()[4]->indexedType()) );
    
    release(context);
  }
  
  {
    QByteArray test = "class A {}; class C {}; enum M { Em }; template<class T> class B{ public:B(T t); }; ";
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 4);
    QCOMPARE(context->childContexts().size(), 5);
    Cpp::TypeConversion conv(context);
    Declaration* decl = findDeclaration(context, QualifiedIdentifier("B<A>"));
    QVERIFY(decl);
    kDebug() << decl->toString();
    
    QVERIFY( conv.implicitConversion(context->localDeclarations()[0]->indexedType(), decl->indexedType()) );
    
    decl = findDeclaration(context, QualifiedIdentifier("B<M>"));
    QVERIFY(decl);
    kDebug() << decl->toString();
    QCOMPARE(context->childContexts()[2]->localDeclarations().size(), 1);
    QVERIFY( conv.implicitConversion(context->childContexts()[2]->localDeclarations()[0]->indexedType(), decl->indexedType()) );    
    
    release(context);
  }  
}

void TestCppCodeCompletion::testInclude() {
  TEST_FILE_PARSE_ONLY

  addInclude("file1.h", "#include \"testFile1.h\"\n#include \"testFile2.h\"\n");


  QByteArray test = "#include \"file1.h\"  \n  struct Cont { operator int() {}; }; void test( int c = 5 ) { this->test( Cont(), 1, 5.5, 6); }; HONK undefinedHonk;";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(c->topContext()->usingImportsCache());
  
  Declaration* decl = findDeclaration(c, QualifiedIdentifier("globalHeinz"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Heinz"));

  decl = findDeclaration(c, QualifiedIdentifier("globalErna"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Erna"));

  decl = findDeclaration(c, QualifiedIdentifier("globalInt"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("int"));

  decl = findDeclaration(c, QualifiedIdentifier("Honk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("honky"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("globalHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("globalMacroHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  ///HONK was #undef'ed in testFile2, so this must be unresolved.
  decl = findDeclaration(c, QualifiedIdentifier("undefinedHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType().cast<DelayedType>());


  Cpp::ExpressionParser parser;

  ///The following test tests the expression-parser, but it is here because the other tests depend on it
  lock.unlock();
  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "globalHonk.erna", DUContextPointer( c ) );
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.type()->toString(), QString("Erna&"));


  ///Test overload-resolution
  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHeinz)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.type()->toString(), QString("Heinz"));

  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHonk.erna)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.type()->toString(), QString("Erna"));

  //No matching function for type Honk. Since the expression-parser is not set to "strict", it returns any found function with the right name.
  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHonk)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  //QCOMPARE(result.type.type()->toString(), QString("Heinz"));


  lock.unlock();
  result = parser.evaluateExpression( "globalFunction(globalHeinz)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.type()->toString(), QString("Heinz"));
  lock.unlock();

  result = parser.evaluateExpression( "globalFunction(globalHonk.erna)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.type()->toString(), QString("Erna"));

  release(c);
}

void TestCppCodeCompletion::testUpdateChain() {
  TEST_FILE_PARSE_ONLY

  DUContext* context = parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );
  parse( testFile3.toUtf8(), DumpNone, 0, KUrl("testIdentity") );


  DUChainWriteLocker lock(DUChain::lock());
  //lock.lock();
  release(context);
}

void TestCppCodeCompletion::testHeaderSections() {
  TEST_FILE_PARSE_ONLY
  /**
   * Make sure that the ends of header-sections are recognized correctly
   * */

  addInclude( "someHeader.h", "\n" );
  addInclude( "otherHeader.h", "\n" );

  IncludeFileList includes;

  HashedString turl("ths.h");

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\nHello", includes, 0, true), QString("\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\nHello", includes, 0, false), QString("\nHello"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\n#include \"otherHeader.h\"\nHello", includes, 0, false), QString("\n\nHello"));
  QCOMPARE(includes.count(), 2);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\n#include \"otherHeader.h\"\nHello", includes, 0, true), QString("\n\n"));
  QCOMPARE(includes.count(), 2);
  includes.clear();

  QCOMPARE(preprocess(turl, "#ifndef GUARD\n#define GUARD\n#include \"someHeader.h\"\nHello\n#endif", includes, 0, true), QString("\n\n\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#ifndef GUARD\n#define GUARD\n#include \"someHeader.h\"\nHello\n#endif", includes, 0, false), QString("\n\n\nHello\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();
}

void TestCppCodeCompletion::testForwardDeclaration()
{
  addInclude( "testdeclaration.h", "class Test{ };" );
  QByteArray method("#include \"testdeclaration.h\"\n class Test; ");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  Declaration* decl = findDeclaration(top, Identifier("Test"), top->range().end);
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  AbstractType::Ptr t(decl->abstractType());
  QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
  QVERIFY(!decl->isForwardDeclaration());

  release(top);
}

void TestCppCodeCompletion::testUsesThroughMacros() {
  {
    QByteArray method("int x;\n#define TEST(X) X\ny = TEST(x);");

    DUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0).start.column, 9);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0).end.column, 10);
  }
  {
    ///2 uses of x, that go through the macro TEST(..), and effectively are in line 2 column 5.
    QByteArray method("int x;\n#define TEST(X) void test() { int z = X; int q = X; }\nTEST(x)");

    kDebug() << method;
    DUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    //Since uses() returns unique uses, and both uses of x in TEST(x) have the same range,
    //only one use is returned.
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 1);

    SimpleRange range1(top->localDeclarations()[0]->uses().begin()->at(0));
    QCOMPARE(range1.start.line, 2);
    QCOMPARE(range1.end.line, 2);
    QCOMPARE(range1.start.column, 5);
    QCOMPARE(range1.end.column, 6);
  }
}

void TestCppCodeCompletion::testAcrossHeaderReferences()
{
  addInclude( "acrossheader1.h", "class Test{ };" );
  addInclude( "acrossheader2.h", "Test t;" );
  QByteArray method("#include \"acrossheader1.h\"\n#include \"acrossheader2.h\"\n");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  Declaration* decl = findDeclaration(top, Identifier("t"), top->range().end);
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  AbstractType::Ptr t(decl->abstractType());
  QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));

  release(top);
}

void TestCppCodeCompletion::testAcrossHeaderTemplateResolution() {
  addInclude("acrossheaderresolution1.h", "class C {}; namespace std { template<class T> class A {  }; }");
  addInclude("acrossheaderresolution2.h", "namespace std { template<class T> class B { typedef A<T> Type; }; }");
  
  QByteArray method("#include \"acrossheaderresolution1.h\"\n#include \"acrossheaderresolution2.h\"\n std::B<C>::Type t;");
  
  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  
  Declaration* decl = findDeclaration(top, QualifiedIdentifier("t"), top->range().end);
  QVERIFY(decl);
  QCOMPARE(QualifiedIdentifier(decl->abstractType()->toString()), QualifiedIdentifier("std::A<C>"));
  
  release(top);
}

void TestCppCodeCompletion::testAcrossHeaderTemplateReferences()
{
  addInclude( "acrossheader1.h", "class Dummy { }; template<class Q> class Test{ };" );
  addInclude( "acrossheader2.h", "template<class B, class B2 = Test<B> > class Test2 : public Test<B>{ Test<B> bm; };" );
  QByteArray method("#include \"acrossheader1.h\"\n#include \"acrossheader2.h\"\n ");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  {
    kDebug() << "top is" << top;
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Dummy"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Dummy"));
  }
  {
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test2<Dummy>::B2"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Test< Dummy >"));
  }
  {
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test2<Dummy>::bm"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Test< Dummy >"));
  }
  {
    Cpp::ClassDeclaration* decl = dynamic_cast<Cpp::ClassDeclaration*>(findDeclaration(top, QualifiedIdentifier("Test2<Dummy>"), top->range().end));
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    CppClassType::Ptr classType = decl->abstractType().cast<CppClassType>();
    QVERIFY(classType);
    QCOMPARE(decl->baseClassesSize(), 1u);
    QVERIFY(decl->baseClasses()[0].baseClass);
    CppClassType::Ptr parentClassType = decl->baseClasses()[0].baseClass.type().cast<CppClassType>();
    QVERIFY(parentClassType);
    QCOMPARE(parentClassType->toString(), QString("Test< Dummy >"));
  }

  release(top);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestCppCodeCompletion::release(DUContext* top)
{
  //EditorIntegrator::releaseTopRange(top->textRangePtr());
  if(dynamic_cast<TopDUContext*>(top))
    DUChain::self()->removeDocumentChain(static_cast<TopDUContext*>(top));
  //delete top;
}

void TestCppCodeCompletion::addInclude( const QString& identity, const QString& text ) {
  fakeIncludes[identity] = text;
}

//Only for debugging
QString print(const Cpp::ReferenceCountedStringSet& set) {
  QString ret;
  bool first = true;
  Cpp::ReferenceCountedStringSet::Iterator it(set.iterator());
  while(it) {
    if(!first)
      ret += ", ";
    first = false;

    ret += (*it).str();
    ++it;
  }
  return ret;
}

//Only for debugging
QStringList toStringList( const Cpp::ReferenceCountedStringSet& set ) {
  QStringList ret;
  Cpp::ReferenceCountedStringSet::Iterator it(set.iterator());
  while(it) {
    ret << (*it).str();
    ++it;
  }
  ret.sort();
  return ret;
}

QStringList splitSorted(const QString& str) {
  QStringList ret = str.split("\n");
  ret.sort();
  return ret;
}

void TestCppCodeCompletion::testEmptyMacroArguments() {
  QString test("#define merge(prefix, suffix) prefix ## suffix\n void merge(test1, ) () { } void merge(, test2) () { }");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 2);
  QCOMPARE(ctx->localDeclarations()[0]->identifier().toString(), QString("test1"));
  QCOMPARE(ctx->localDeclarations()[1]->identifier().toString(), QString("test2"));
}

void TestCppCodeCompletion::testMacroExpansionRanges() {
{
  QString test("#define TEST(X) int allabamma; \nTEST(C)\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  //kDebug() << ctx->localDeclarations()[1]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 7, 1, 7)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
//  QCOMPARE(ctx->localDeclarations()[1]->range().textRange(), KTextEditor::Range(1, 10, 1, 11));
  //kDebug() << "Range:" << ctx->localDeclarations()[0]->range().textRange();
}
{
  QString test("#define A(X) bbbbbb\nint A(0);\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 8, 1, 8)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
}
{
  QString test("#define TEST namespace NS{int a;int b;int c;int d;int q;} class A{}; \nTEST; int a; int b; int c; int d;int e;int f;int g;int h;\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 10);
  QCOMPARE(ctx->localDeclarations()[1]->range().textRange(), KTextEditor::Range(1, 4, 1, 4)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
  QCOMPARE(ctx->localDeclarations()[2]->range().textRange(), KTextEditor::Range(1, 10, 1, 11));
}
{
  //The range of the merged declaration name should be trimmed to the end of the macro invocation
  QString test("#define TEST(X) class X ## Class {};\nTEST(Hallo)\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 5, 1, 11));
}
}

void TestCppCodeCompletion::testNaiveMatching() {
  return;
    Cpp::EnvironmentManager::setMatchingLevel(Cpp::EnvironmentManager::Naive);
    {
      addInclude("recursive_test_1.h", "#include \"recursive_test_2.h\"\nint i1;\n");
      addInclude("recursive_test_2.h", "#include \"recursive_test_1.h\"\nint i2;\n");
      
      TopDUContext* test1 = parse(QByteArray("#include \"recursive_test_1.h\"\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      QCOMPARE(test1->recursiveImportIndices().count(), 3u);
      QCOMPARE(test1->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }

}

void TestCppCodeCompletion::testHeaderGuards() {
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n#define BLA\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5;\n#ifndef GUARD\n#define GUARD\n#endif\n#if defined(TEST)\n int q = 4;#endif\n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QCOMPARE(envFile1->headerGuard().str(), QString("GUARD"));
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("int x;\n#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#define X\n#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\nint o;\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
}

void TestCppCodeCompletion::testEnvironmentMatching() {
    {
      CppPreprocessEnvironment::setRecordOnlyImportantString(false);
      
      addInclude("deep2.h", "#ifdef WANT_DEEP\nint x;\n#undef WANT_DEEP\n#endif\n");
      addInclude("deep1.h", "#define WANT_DEEP\n#include \"deep2.h\"\n");
      TopDUContext* test1 = parse(QByteArray("#include \"deep1.h\""), DumpNone);
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QCOMPARE(envFile1->definedMacroNames().set().count(), 0u);
      QCOMPARE(envFile1->definedMacros().set().count(), 0u);
      QCOMPARE(envFile1->usedMacros().set().count(), 0u);
    }

    addInclude("h1.h", "#ifndef H1_H  \n#define H1_H  \n  class H1 {};\n #else \n class H1_Already_Defined {}; \n#endif");
    addInclude("h1_user.h", "#ifndef H1_USER \n#define H1_USER \n#include \"h1.h\" \nclass H1User {}; \n#endif\n");

    {
      TopDUContext* test1 = parse(QByteArray("#include \"h1.h\" \n#include \"h1_user.h\"\n\nclass Honk {};"), DumpNone);
        //We test here, whether the environment-manager re-combines h1_user.h so it actually contains a definition of class H1.
        //In the version parsed in test1, H1_H was already defined, so the h1.h parsed into h1_user.h was parsed to contain H1_Already_Defined.
        TopDUContext* test2 = parse(QByteArray("#include \"h1_user.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(test1->parsingEnvironmentFile());
        QVERIFY(test2->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(test2->parsingEnvironmentFile().data());
        QVERIFY(envFile1);
        QVERIFY(envFile2);

        QCOMPARE(envFile1->usedMacros().set().count(), 0u);
        QCOMPARE(envFile2->usedMacros().set().count(), 0u);
        QVERIFY(findDeclaration( test1, Identifier("H1") ));

      QCOMPARE( envFile1->contentStartLine(), 3 );
    }

    { //Test shadowing of strings through #undefs
      addInclude("stringset_test1.h", "String1 s1;\n#undef String2\n String2 s2;");
      addInclude("stringset_test2.h", "String1 s1;\n#undef String2\n String2 s2;");

      {
        TopDUContext* top = parse(QByteArray("#include \"stringset_test1.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        kDebug() << "url" << envFile->url().str();
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        QCOMPARE(toStringList(envFile->strings()), splitSorted("String1\ns1\ns2")); //The #undef protects String2, so it cannot be affected from outside
      }
      {
        TopDUContext* top = parse(QByteArray("#define String1\n#include \"stringset_test1.h\"\nString2 String1;"), DumpNone); //Both String1 and String2 are shadowed. String1 by the macro, and String2 by the #undef in stringset_test1.h
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        //String1 is shadowed by the macro-definition, so it is not a string that can be affected from outside.
        QCOMPARE(toStringList(envFile->strings()), splitSorted("s1\ns2"));
        QCOMPARE(toStringList(envFile->usedMacroNames()), QStringList()); //No macros from outside were used

        QCOMPARE(envFile->definedMacros().set().count(), 1u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);

        QCOMPARE(envFile2->definedMacros().set().count(), 0u);

        QCOMPARE(toStringList(envFile2->usedMacroNames()), QStringList("String1")); //stringset_test1.h used the Macro String1 from outside
        QCOMPARE(toStringList(envFile2->strings()), splitSorted("String1\ns1\ns2"));
      }
      {
        TopDUContext* top = parse(QByteArray("#define String1\n#undef String1\n#include \"stringset_test1.h\""), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        QCOMPARE(envFile->definedMacros().set().count(), 0u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        //String1 is shadowed by the macro-definition, so it is not a string that can be affected from outside.
        kDebug() << toStringList(envFile->strings()) << splitSorted("s1\ns2");
        QCOMPARE(toStringList(envFile->strings()), splitSorted("s1\ns2"));
        QCOMPARE(toStringList(envFile->usedMacroNames()), QStringList()); //No macros from outside were used

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);
        QCOMPARE(envFile2->definedMacros().set().count(), 0u);

        QCOMPARE(toStringList(envFile2->usedMacroNames()), QStringList()); //stringset_test1.h used the Macro String1 from outside. However it is an undef-macro, so it does not appear in usedMacroNames() and usedMacros()
        QCOMPARE(envFile2->usedMacros().set().count(), (unsigned int)0);
        QCOMPARE(toStringList(envFile2->strings()), splitSorted("String1\ns1\ns2"));
      }
      {
        addInclude("usingtest1.h", "#define HONK\nMACRO m\n#undef HONK2\n");

        TopDUContext* top = parse(QByteArray("#define MACRO meh\nint MACRO;\n#include \"usingtest1.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        QCOMPARE(envFile->definedMacros().set().count(), 2u);
        QCOMPARE(envFile->unDefinedMacroNames().set().count(), 1u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        QCOMPARE(envFile->usedMacroNames().set().count(), 0u);

        kDebug() << toStringList(envFile->strings()) ;
        QCOMPARE(envFile->strings().count(), 3u); //meh, m, int

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);
        QCOMPARE(envFile2->definedMacros().set().count(), 1u);
        QCOMPARE(envFile2->unDefinedMacroNames().set().count(), 1u);
        QCOMPARE(envFile2->usedMacros().set().count(), 1u);
        QCOMPARE(envFile2->usedMacroNames().set().count(), 1u);
        kDebug() << toStringList(envFile2->strings()) ;
        QCOMPARE(envFile2->strings().count(), 3u); //meh(from macro), MACRO, m
      }
    }

/*    addInclude( "envmatch_header1.h", "#include \"envmatch_header2.h\"\n class SomeName; #define SomeName SomeAlternativeName" );
    addInclude( "envmatch_header2.h", "#ifndef SOMEDEF\n #define SOMEDEF\n#endif\n" );
    QByteArray method1("#include \"envmatch_header1.h\"");
    QByteArray method2("#include \"envmatch_header1.h\"");
    QByteArray method3("#include \"envmatch_header1.h\"\n#include \"envmatch_header1.h\"");

    DUContext* top1 = parse(method1, DumpNone);
    DUContext* top2 = parse(method1, DumpNone);
    DUContext* top3 = parse(method1, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top1->importedParentContexts().count(), 1);
    QCOMPARE(top2->importedParentContexts().count(), 1);
//     QCOMPARE(top3->importedParentContexts().count(), 2);

    QCOMPARE(top1->importedParentContexts()[0], top2->importedParentContexts()[1]);*/
}

void TestCppCodeCompletion::testPreprocessor() {
  TEST_FILE_PARSE_ONLY
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define D(X,Y) X ## Y \nint D(a,ba);"), DumpNone);
    IncludeFileList includes;
    kDebug() << preprocess(HashedString("somefile"), "#define D(X,Y) X ## Y \nint D(a,ba);", includes);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("aba"));
  }
  {
    TopDUContext* top = parse(QByteArray("#define MERGE(a, b) a ## b \n#define MERGE_WITH_PARENS(par) MERGE ## par \nint MERGE_WITH_PARENS((int, B));"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("intB"));
  }
  {//Test simple #if
    TopDUContext* top = parse(QByteArray("#define X\n#if defined(X)\nint xDefined;\n#endif\n#if !defined(X)\nint xNotDefined;\n#endif\n#if (!defined(X))\nint xNotDefined2;\n#endif"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("xDefined"));
  }
  {//Test simple #if
    TopDUContext* top = parse(QByteArray("#if defined(X)\nint xDefined;\n#endif\n#if !defined(X)\nint xNotDefined;\n#endif\n#if (!defined(X))\nint xNotDefined2;\n#endif"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(top->localDeclarations().count() >= 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("xNotDefined"));
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("xNotDefined2"));
  }
  {//Test multi-line definitions
    TopDUContext* top = parse(QByteArray("#define X \\\nint i;\\\nint o;\nX;\n"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
  }
  {//Test multi-line definitions
    TopDUContext* top = parse(QByteArray("#define THROUGH_DEFINE(X) X\nclass B {\nclass C{\n};\nC* cPcPcPcPcPcPcPcPcP;\n};\nB* bP;\nvoid test() {\nTHROUGH_DEFINE(bP->cPcPcPcPcPcPcPcPcP);\n}\n"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0).start.column, 19);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0).end.column, 37);
  }
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define D(X,Y) X ## Y \nint D(a,ba);"), DumpNone);
    IncludeFileList includes;
    kDebug() << preprocess(HashedString("somefile"), "#define D(X,Y) X ## Y \nint D(a,ba);", includes);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("aba"));
  }
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define A(x) int x;\n#define B(name) A(bo ## name)\nB(Hallo)"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("boHallo"));
  }
}


struct TestPreprocessor : public rpp::Preprocessor {

  TestCppCodeCompletion* cc;
  IncludeFileList& included;
  rpp::pp* pp;
  bool stopAfterHeaders;
  Cpp::EnvironmentFilePointer environmentFile;

  TestPreprocessor( TestCppCodeCompletion* _cc, IncludeFileList& _included, bool _stopAfterHeaders ) : cc(_cc), included(_included), pp(0), stopAfterHeaders(_stopAfterHeaders) {
  }

  rpp::Stream* sourceNeeded(QString& fileName, rpp::Preprocessor::IncludeType type, int sourceLine, bool skipCurrentPath)
  {
    QMap<QString,QString>::const_iterator it = cc->fakeIncludes.find(fileName);
    if( it != cc->fakeIncludes.end() || !pp ) {
      kDebug(9007) << "parsing included file \"" << fileName << "\"";
      included << LineContextPair( dynamic_cast<TopDUContext*>(cc->parse( (*it).toUtf8(), TestCppCodeCompletion::DumpNone, pp, KUrl(it.key()))), sourceLine );
    } else {
      kDebug(9007) << "could not find include-file \"" << fileName << "\"";
    }
    return 0;
  }

  void setPp( rpp::pp* _pp ) {
    pp = _pp;
  }

  virtual void headerSectionEnded(rpp::Stream& stream) {
    if( environmentFile )
      environmentFile->setContentStartLine( stream.originalInputPosition().line );
    if(stopAfterHeaders)
      stream.toEnd();
  }
  
  virtual void foundHeaderGuard(rpp::Stream& stream, KDevelop::IndexedString guardName) {
    environmentFile->setHeaderGuard(guardName);
  }
};

QString TestCppCodeCompletion::preprocess( const HashedString& url, const QString& text, IncludeFileList& included, rpp::pp* parent, bool stopAfterHeaders, KSharedPtr<Cpp::EnvironmentFile>* paramEnvironmentFile, rpp::LocationTable** returnLocationTable, PreprocessedContents* targetContents ) {
  TestPreprocessor ppc( this, included, stopAfterHeaders );


    rpp::pp preprocessor(&ppc);
    ppc.setPp( &preprocessor );

    KSharedPtr<Cpp::EnvironmentFile> environmentFile;
    if( paramEnvironmentFile && *paramEnvironmentFile )
      environmentFile = *paramEnvironmentFile;
    else
      environmentFile = Cpp::EnvironmentFilePointer( new Cpp::EnvironmentFile( IndexedString(url.str()), 0 ) );

  ppc.environmentFile = environmentFile;

    if( paramEnvironmentFile )
      *paramEnvironmentFile = environmentFile;

    CppPreprocessEnvironment* currentEnvironment = new CppPreprocessEnvironment( &preprocessor, environmentFile );
    preprocessor.setEnvironment( currentEnvironment );
    currentEnvironment->setEnvironmentFile( environmentFile );

    rpp::MacroBlock* macros = 0;
    if(parent)
      macros = parent->environment()->firstBlock();
    else
      macros = new rpp::MacroBlock(0);

    preprocessor.environment()->enterBlock(macros);

    if( parent )
      preprocessor.environment()->swapMacros(parent->environment());

    PreprocessedContents contents = preprocessor.processFile("<test>", text.toUtf8());
    if(targetContents)
      *targetContents = contents;

    QString result = QString::fromUtf8(stringFromContents(contents));

    if (returnLocationTable)
      *returnLocationTable = preprocessor.environment()->takeLocationTable();

    currentEnvironment->finishEnvironment();

    if( parent ) {
      preprocessor.environment()->swapMacros(parent->environment());
      static_cast<CppPreprocessEnvironment*>(parent->environment())->environmentFile()->merge(*environmentFile);
    }

    return result;
}

TopDUContext* TestCppCodeCompletion::parse(const QByteArray& unit, DumpAreas dump, rpp::pp* parent, KUrl _identity, TopDUContext* update)
{
  if (dump)
    kDebug(9007) << "==== Beginning new test case...:" << endl << unit;

  ParseSession* session = new ParseSession();
   ;

  static int testNumber = 0;
  HashedString url(QString("file:///internal/%1").arg(testNumber++));
  if( !_identity.isEmpty() )
      url = _identity.pathOrUrl();

   IncludeFileList included;
   QList<DUContext*> temporaryIncluded;

  rpp::LocationTable* locationTable;

  Cpp::EnvironmentFilePointer file;

  PreprocessedContents contents;

  preprocess( url, QString::fromUtf8(unit), included, parent, false, &file, &locationTable, &contents ).toUtf8();

  session->setContents( contents, locationTable );

    if( parent ) {
      //Temporarily insert all files parsed previously by the parent, so forward-declarations can be resolved etc.
      TestPreprocessor* testPreproc = dynamic_cast<TestPreprocessor*>(parent->preprocessor());
      if( testPreproc ) {
        foreach( LineContextPair include, testPreproc->included ) {
          if( !containsContext( included, include.context ) ) {
            included.push_front( include );
            temporaryIncluded << include.context;
          }
        }
      } else {
        kDebug(9007) << "PROBLEM";
      }
    }

  Parser parser(&control);
  TranslationUnitAST* ast = parser.parse(session);
  ast->session = session;

  if (dump & DumpAST) {
    kDebug(9007) << "===== AST:";
    cppDumper.dump(ast, session);
  }

  DeclarationBuilder definitionBuilder(session);

  TopDUContext* top = definitionBuilder.buildDeclarations(file, ast, &included, ReferencedTopDUContext(update));

  UseBuilder useBuilder(session);
  useBuilder.buildUses(ast);

  if (dump & DumpDUChain) {
    kDebug(9007) << "===== DUChain:";

    DUChainWriteLocker lock(DUChain::lock());
    dumper.dump(top);
  }

  if (dump & DumpType) {
    kDebug(9007) << "===== Types:";
    DumpTypes dt;
    DUChainWriteLocker lock(DUChain::lock());
    foreach (const AbstractType::Ptr& type, definitionBuilder.topTypes())
      dt.dump(type.unsafeData());
  }

  if( parent ) {
    //Remove temporarily inserted files parsed previously by the parent
    DUChainWriteLocker lock(DUChain::lock());
    TestPreprocessor* testPreproc = dynamic_cast<TestPreprocessor*>(parent->preprocessor());
    if( testPreproc ) {
      foreach( DUContext* context, temporaryIncluded )
        top->removeImportedParentContext( context );
    } else {
      kDebug(9007) << "PROBLEM";
    }
  }


  if (dump)
    kDebug(9007) << "===== Finished test case.";

  delete session;

  return top;
}

#include "test_cppcodecompletion.moc"