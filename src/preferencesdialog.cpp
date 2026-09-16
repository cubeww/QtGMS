#include "preferencesdialog.h"
#include "editorlanguage.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent) : EditorDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    auto *layout = new QVBoxLayout(bodyWidget());
    auto *generalGroup = new QGroupBox(tr("General"));
    auto *form = new QFormLayout(generalGroup);
    auto *languageCombo = new QComboBox;
    languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageCombo->addItem(QString::fromUtf8("简体中文"), QStringLiteral("zh_CN"));
    languageCombo->addItem(QString::fromUtf8("日本語"), QStringLiteral("ja_JP"));
    languageCombo->addItem(QString::fromUtf8("한국어"), QStringLiteral("ko_KR"));
    languageCombo->setCurrentIndex(languageCombo->findData(EditorLanguage::selectedLanguage()));
    form->addRow(tr("Language:"), languageCombo);
    auto *hint = new QLabel(tr("Language changes take effect the next time you start QtGMS."));
    hint->setWordWrap(true);
    form->addRow(hint);
    layout->addWidget(generalGroup);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, languageCombo] {
        EditorLanguage::setSelectedLanguage(languageCombo->currentData().toString());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(380, 185);
}
