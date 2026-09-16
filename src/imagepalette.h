#ifndef QTGMS_IMAGEPALETTE_H
#define QTGMS_IMAGEPALETTE_H
#include <QWidget>
#include <QColor>
#include <QVector>
class ImagePalette : public QWidget
{
    Q_OBJECT
public:
    explicit ImagePalette(QWidget *parent = nullptr);
    bool load(const QString &path, QString &error);
    bool save(const QString &path, QString &error) const;
signals:
    void colorSelected(const QColor &color, bool secondary);
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    void pick(const QPoint &position, Qt::MouseButton button);
    QVector<QColor> m_colors;
};
#endif
