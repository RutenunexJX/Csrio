#include "ElaCheckBox.h"

#include "ElaApplication.h"
#include "ElaCheckBoxStyle.h"
ElaCheckBox::ElaCheckBox(QWidget* parent)
    : QCheckBox(parent)
{
    _pBorderRadius = 3;
    setMouseTracking(true);
    setObjectName("ElaCheckBox");
    _ownedStyle = new ElaCheckBoxStyle(style());
    setStyle(_ownedStyle);
    QFont font = this->font();
    font.setPixelSize(eApp->getFontPixelSize() + 2);
    setFont(font);
}

ElaCheckBox::ElaCheckBox(const QString& text, QWidget* parent)
    : ElaCheckBox(parent)
{
    setText(text);
}

ElaCheckBox::~ElaCheckBox()
{
    setStyle(nullptr);
    delete _ownedStyle;
}
