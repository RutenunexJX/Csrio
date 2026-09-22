#pragma once

#include <QListWidget>
#include "ElaWidgetToolsExport.h"

class ElaListViewStyle;

// Ela rendering with QListWidget's existing item/model contract.
class ELA_EXPORT ElaListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ElaListWidget(QWidget* parent = nullptr);
    ~ElaListWidget() override;
private:
    ElaListViewStyle* _listStyle;
};
