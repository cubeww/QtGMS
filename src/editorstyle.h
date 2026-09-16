#ifndef QTGMS_EDITORSTYLE_H
#define QTGMS_EDITORSTYLE_H

#include <QProxyStyle>

class EditorStyle : public QProxyStyle
{
public:
    EditorStyle();
    using QProxyStyle::polish;
    void polish(QWidget *widget) override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override;
};

#endif
