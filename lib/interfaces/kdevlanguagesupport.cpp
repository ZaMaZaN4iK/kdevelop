#include "codemodel.h"

#include "kdevlanguagesupport.h"

KDevLanguageSupport::KDevLanguageSupport( const QString& pluginName, const QString& icon, QObject *parent, const char *name)
    : KDevPlugin( pluginName, icon, parent, name ? name : "KDevLanguageSupport" )
{
}

KDevLanguageSupport::~KDevLanguageSupport()
{
}

KDevLanguageSupport::Features KDevLanguageSupport::features()
{
    return Features(0);
}

KMimeType::List KDevLanguageSupport::mimeTypes()
{
    return KMimeType::List();
}

QString KDevLanguageSupport::formatTag( const Tag& tag )
{
// class Tag is undefined therefore gcc-2.x does not want cast it - let then there be "unused" warning for gcc<3.0 !
#if __GNUC__ >= 3
    Q_UNUSED( tag );
#endif
    return QString::null;
}

QString KDevLanguageSupport::formatClassName(const QString &name)
{
    return name;
}

QString KDevLanguageSupport::unformatClassName(const QString &name)
{
    return name;
}

void KDevLanguageSupport::addClass()
{
}

void KDevLanguageSupport::addMethod( ClassDom klass )
{
    Q_UNUSED( klass );
}

void KDevLanguageSupport::implementVirtualMethods( ClassDom klass )
{
    Q_UNUSED( klass );
}

void KDevLanguageSupport::addAttribute( ClassDom klass )
{
    Q_UNUSED( klass );
}

QStringList KDevLanguageSupport::subclassWidget(const QString& /*formName*/)
{
    return QStringList();
}

QStringList KDevLanguageSupport::updateWidget(const QString& /*formName*/, const QString& /*fileName*/)
{
    return QStringList();
}

QString KDevLanguageSupport::formatModelItem( const CodeModelItem *item, bool shortDescription )
{
    Q_UNUSED( shortDescription );
    return item->name();
}

#include "kdevlanguagesupport.moc"
