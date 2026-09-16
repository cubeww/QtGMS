#include "editorstandarddialogs.h"
#include "fontrangedialog.h"
#include "textfiledocument.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>

FontRangeDialog::FontRangeDialog(const std::function<bool(QString &, QString &)> &codeCharacters, QWidget *parent)
    : EditorDialog(parent), m_codeCharacters(codeCharacters), m_first(new QSpinBox(this)),
    m_last(new QSpinBox(this)), m_characters(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Font Range")); resize(330, 280);
    auto *layout = new QGridLayout(bodyWidget()); auto *group = new QGroupBox(tr("From Range"), this);
    auto *rangeLayout = new QGridLayout(group);
    m_first->setRange(0, 65535); m_last->setRange(0, 65535); m_first->setValue(32); m_last->setValue(127);
    rangeLayout->addWidget(m_first, 0, 0); rangeLayout->addWidget(new QLabel(tr("till")), 0, 1); rangeLayout->addWidget(m_last, 0, 2);
    const QStringList labels = {tr("Normal"), tr("ASCII"), tr("Digits"), tr("Letters")};
    const int starts[] = {32, 0, 48, 65}, ends[] = {127, 255, 57, 122};
    for (int i = 0; i < 4; ++i) {
        auto *button = new QPushButton(labels.at(i), group); rangeLayout->addWidget(button, 1 + i / 2, (i % 2) * 2);
        const int first = starts[i], last = ends[i];
        connect(button, &QPushButton::clicked, this, [this, first, last] { m_first->setValue(first); m_last->setValue(last); updateCharacters(); });
    }
    layout->addWidget(group, 0, 0, 2, 1);
    auto *code = new QPushButton(tr("From Code"), this);
    code->setToolTip(tr("Collect characters from strings in the project code."));
    connect(code, &QPushButton::clicked, this, &FontRangeDialog::loadCodeCharacters);
    auto *file = new QPushButton(tr("From File"), this); layout->addWidget(code, 0, 1); layout->addWidget(file, 1, 1);
    layout->addWidget(m_characters, 2, 0, 1, 2); m_characters->setToolTip(tr("Type or paste the characters to include."));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons, 3, 0, 1, 2);
    connect(buttons, &QDialogButtonBox::accepted, this, &FontRangeDialog::acceptRanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(file, &QPushButton::clicked, this, &FontRangeDialog::loadCharacters);
    for (QSpinBox *spin : {m_first, m_last}) {
        spin->setKeyboardTracking(false);
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { updateCharacters(); });
    }
    connect(m_characters, &QPlainTextEdit::textChanged, this, [this] { m_fromText = true; }); updateCharacters();
}
void FontRangeDialog::updateCharacters()
{
    QSignalBlocker blocker(m_characters); QString text;
    for (int code = m_first->value(); code <= m_last->value() && text.size() < 4096; ++code)
        if (QChar(static_cast<ushort>(code)).isPrint()) text.append(QChar(static_cast<ushort>(code)));
    m_characters->setPlainText(text); m_fromText = false;
}
void FontRangeDialog::loadCodeCharacters()
{
    QString characters, error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool success = m_codeCharacters(characters, error);
    QApplication::restoreOverrideCursor();
    if (!success) { EditorMessageBox::critical(this, tr("Cannot Scan Project Code"), error); return; }
    m_characters->setPlainText(characters);
    m_fromText = true;
    if (characters.isEmpty())
        EditorMessageBox::information(this, tr("From Code"), tr("No string characters were found in the project code."));
}
void FontRangeDialog::loadCharacters()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Characters From File"), QString(), tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString text, error; if (!TextFileDocument::readText(path, text, error)) { EditorMessageBox::critical(this, tr("Cannot Read Text"), error); return; }
    m_characters->setPlainText(text); m_fromText = true;
}
void FontRangeDialog::acceptRanges()
{
    m_ranges.clear();
    if (!m_fromText) {
        if (m_first->value() > m_last->value()) { EditorMessageBox::warning(this, tr("Font Range"), tr("The first character must not exceed the last character.")); return; }
        FontRange range; range.first = m_first->value(); range.last = m_last->value(); m_ranges.append(range);
    } else {
        QSet<uint> unique;
        for (uint code : m_characters->toPlainText().toUcs4()) {
            if (code > 65535 || (code >= 0xd800 && code <= 0xdfff)) {
                EditorMessageBox::warning(this, tr("Font Range"), tr("GameMaker 1.4 font ranges support character values from 0 to 65535.")); return;
            }
            unique.insert(code);
        }
        QList<uint> values = unique.values(); std::sort(values.begin(), values.end());
        for (uint code : values) {
            if (!m_ranges.isEmpty() && m_ranges.last().last + 1 == int(code)) m_ranges.last().last = int(code);
            else { FontRange range; range.first = range.last = int(code); m_ranges.append(range); }
        }
    }
    accept();
}
