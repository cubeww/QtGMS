#include <QSignalBlocker>
#include "editorstandarddialogs.h"
#include "soundpropertieswindow.h"
#include "sounddocument.h"
#include "audiopreview.h"
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QSlider>
#include <QVBoxLayout>

static int soundQuality(int bitRate)
{
    const int limits[] = {48, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    for (int i = 0; i < 10; ++i) if (bitRate <= limits[i]) return i;
    return 10;
}

SoundPropertiesWindow::SoundPropertiesWindow(SoundDocument *document, const QStringList &audioGroups, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_preview(new AudioPreview(this)), m_fileName(new QLabel),
      m_attributes(new QButtonGroup(this)), m_volume(new QSlider(Qt::Horizontal)), m_quality(new QSlider(Qt::Horizontal)),
      m_channels(new QComboBox), m_sampleRate(new QComboBox), m_bitRate(new QComboBox), m_bitDepth(new QComboBox),
      m_audioGroup(new QComboBox), m_playButton(new QPushButton(QIcon(QStringLiteral(":/images/run.png")), QString())),
      m_stopButton(new QPushButton(QIcon(QStringLiteral(":/images/stop.png")), QString()))
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide();
    resize(493, 372); setMinimumSize(493, 372);
    auto *body = new QWidget(this); auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 10); layout->setSpacing(8);
    auto *top = new QHBoxLayout; top->setSpacing(6);
    auto *nameLabel = new QLabel(tr("&Name:")); auto *nameEdit = new QLineEdit(document->name());
    enableResourceRenaming(nameEdit, ResourceType::Sound); nameLabel->setBuddy(nameEdit);
    auto *load = new QPushButton(QIcon(QStringLiteral(":/images/open.png")), QString()); load->setToolTip(tr("Load sound from a file"));
    top->addWidget(nameLabel); top->addWidget(nameEdit, 1);
    for (QPushButton *button : {load, m_playButton, m_stopButton}) { button->setFixedSize(27, 24); top->addWidget(button); }
    m_playButton->setToolTip(tr("Play the source sample")); m_stopButton->setToolTip(tr("Stop the sound"));
    layout->addLayout(top); layout->addWidget(m_fileName);
    auto *attributes = new QGroupBox(tr("Attributes")); auto *attributeLayout = new QVBoxLayout(attributes);
    attributeLayout->setContentsMargins(8, 14, 8, 6); attributeLayout->setSpacing(2);
    const QStringList modes = {tr("Uncompressed - Not Streamed (In Memory, low CPU)"),
        tr("Compressed - Not Streamed (In Memory, higher CPU)"),
        tr("Compressed (Uncompress on load) - Not Streamed (Higher Memory, low CPU)"),
        tr("Compressed - Streamed (On Disk, higher CPU)")};
    for (int i = 0; i < modes.size(); ++i) {
        auto *radio = new QRadioButton(modes.at(i), attributes); m_attributes->addButton(radio, i); attributeLayout->addWidget(radio);
    }
    layout->addWidget(attributes);
    auto *sliders = new QHBoxLayout;
    sliders->addWidget(new QLabel(tr("Quality:"))); sliders->addWidget(m_quality, 1);
    sliders->addWidget(new QLabel(tr("Volume:"))); sliders->addWidget(m_volume, 1);
    m_quality->setRange(0, 10); m_volume->setRange(0, 100);
    for (QSlider *slider : {m_quality, m_volume}) { slider->setTickPosition(QSlider::TicksBelow); slider->setPageStep(1); }
    m_quality->setTickInterval(1); m_volume->setTickInterval(10); m_volume->setPageStep(10);
    layout->addLayout(sliders);
    auto *lower = new QHBoxLayout;
    auto *target = new QGroupBox(tr("Target Options")); auto *options = new QGridLayout(target);
    options->setContentsMargins(10, 16, 10, 8); options->setSpacing(6);
    m_channels->addItems({tr("Mono"), tr("Stereo"), tr("3D")});
    m_bitDepth->addItem(tr("8 bit"), 8); m_bitDepth->addItem(tr("16 bit"), 16);
    m_sampleRate->addItems({QStringLiteral("5512"), QStringLiteral("11025"), QStringLiteral("22050"), QStringLiteral("32000"), QStringLiteral("44100"), QStringLiteral("48000")});
    m_sampleRate->setEditable(true); m_sampleRate->setInsertPolicy(QComboBox::NoInsert);
    m_sampleRate->setValidator(new QIntValidator(1, 192000, m_sampleRate));
    for (int rate : {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 192, 224, 256, 320, 512})
        m_bitRate->addItem(QString::number(rate), rate);
    if (m_bitRate->findData(document->state().bitRate) < 0)
        m_bitRate->addItem(QString::number(document->state().bitRate), document->state().bitRate);
    options->addWidget(m_channels, 0, 0); options->addWidget(new QLabel(tr("Sample Rate")), 0, 1); options->addWidget(m_sampleRate, 0, 2);
    options->addWidget(m_bitDepth, 1, 0); options->addWidget(new QLabel(tr("Bit Rate (kbps)")), 1, 1); options->addWidget(m_bitRate, 1, 2);
    lower->addWidget(target, 1);
    auto *buttons = new QVBoxLayout;
    auto *edit = new QPushButton(tr("&Edit Sound")); edit->setEnabled(false); edit->setToolTip(tr("External sound editor configuration is not available yet."));
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK"));
    buttons->addWidget(edit); buttons->addWidget(ok); lower->addLayout(buttons);
    layout->addLayout(lower);
    auto *group = new QHBoxLayout; group->addWidget(new QLabel(tr("Audio Group:"))); group->addWidget(m_audioGroup, 1); group->addStretch();
    for (int i = 0; i < audioGroups.size(); ++i) m_audioGroup->addItem(audioGroups.at(i), i);
    if (m_audioGroup->findData(document->state().audioGroup) < 0)
        m_audioGroup->addItem(QString::number(document->state().audioGroup), document->state().audioGroup);
    layout->addLayout(group); layout->addStretch(); setCentralWidget(body);
    connect(load, &QPushButton::clicked, this, &SoundPropertiesWindow::loadAudio);
    connect(m_playButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!m_preview->play(m_document->state().audio, m_volume->value() / 100.0, error))
            EditorMessageBox::warning(this, tr("Cannot Play Sound"), error);
    });
    connect(m_stopButton, &QPushButton::clicked, m_preview, &AudioPreview::stop);
    connect(m_preview, &AudioPreview::playingChanged, this, [this](bool playing) { m_stopButton->setEnabled(playing); });
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(m_attributes, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), this, [this] { applySettings(); });
    for (QComboBox *combo : {m_channels, m_bitDepth, m_bitRate, m_audioGroup, m_sampleRate})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this] { applySettings(); });
    connect(m_sampleRate->lineEdit(), &QLineEdit::editingFinished, this, &SoundPropertiesWindow::applySettings);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        m_volume->setToolTip(tr("Volume: %1%").arg(value)); m_preview->setVolume(value / 100.0);
        if (!m_volume->isSliderDown()) applySettings();
    });
    connect(m_volume, &QSlider::sliderReleased, this, &SoundPropertiesWindow::applySettings);
    connect(m_quality, &QSlider::valueChanged, this, [this](int value) {
        if (m_refreshing) return;
        const int rates[] = {48, 80, 96, 112, 128, 160, 192, 224, 256, 320, 512};
        m_bitRate->setCurrentIndex(m_bitRate->findData(rates[value]));
        m_quality->setToolTip(tr("Quality: %1").arg(value));
        if (!m_quality->isSliderDown()) applySettings();
    });
    connect(m_quality, &QSlider::sliderReleased, this, &SoundPropertiesWindow::applySettings);
    connect(document, &SoundDocument::changed, this, &SoundPropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { refresh(); });
    connect(document, &SoundDocument::saved, this, [this] { emit resourceSaved(ResourceType::Sound, m_document->filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &SoundPropertiesWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, document->undoStack(), &QUndoStack::undo); connect(redo, &QShortcut::activated, document->undoStack(), &QUndoStack::redo);
    refresh();
}
QString SoundPropertiesWindow::filePath() const { return m_document->filePath(); }
void SoundPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_preview->stop(); m_document->relocate(oldDirectory, newDirectory); }
bool SoundPropertiesWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && m_preview) m_preview->stop();
    return ResourceEditorWindow::event(event);
}
void SoundPropertiesWindow::refresh()
{
    m_refreshing = true;
    const SoundState &state = m_document->state();
    if (state.audio != m_previewAudio) { m_preview->stop(); m_previewAudio = state.audio; }
    setWindowTitle(tr("Sound Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    const QString filename = QFileInfo(QString(state.originalName).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    m_fileName->setText(tr("Filename: %1").arg(filename)); m_fileName->setToolTip(state.originalName);
    m_attributes->button(state.attributes)->setChecked(true);
    m_volume->setValue(qRound(state.volume * 100)); m_preview->setVolume(state.volume);
    m_quality->setValue(soundQuality(state.bitRate));
    m_quality->setEnabled(state.attributes != 0); m_bitRate->setEnabled(state.attributes != 0);
    m_bitDepth->setEnabled(state.attributes == 0);
    m_channels->setCurrentIndex(state.channelType); m_sampleRate->setEditText(QString::number(state.sampleRate));
    m_bitDepth->setCurrentIndex(m_bitDepth->findData(state.bitDepth)); m_bitRate->setCurrentIndex(m_bitRate->findData(state.bitRate));
    m_audioGroup->setCurrentIndex(m_audioGroup->findData(state.audioGroup));
    m_playButton->setEnabled(!state.audio.isEmpty()); m_stopButton->setEnabled(m_preview->isPlaying());
    m_refreshing = false;
}
void SoundPropertiesWindow::applySettings()
{
    if (m_refreshing || !m_sampleRate->lineEdit()->hasAcceptableInput()) return;
    SoundState state = m_document->state();
    const int attributes = m_attributes->checkedId(); const double volume = m_volume->value() / 100.0;
    const int rate = m_sampleRate->currentText().toInt(), bits = m_bitDepth->currentData().toInt();
    const int channels = m_channels->currentIndex(), bitRate = m_bitRate->currentData().toInt(), group = m_audioGroup->currentData().toInt();
    if (state.attributes == attributes && qRound(state.volume * 100) == m_volume->value() && state.sampleRate == rate
        && state.bitDepth == bits && state.channelType == channels && state.bitRate == bitRate && state.audioGroup == group) return;
    state.attributes = attributes;
    if (qRound(state.volume * 100) != m_volume->value()) state.volume = volume;
    state.sampleRate = rate; state.bitDepth = bits;
    state.channelType = channels; state.bitRate = bitRate; state.audioGroup = group;
    m_document->edit(state, tr("Change sound properties"));
}
void SoundPropertiesWindow::loadAudio()
{
    m_preview->stop();
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Sound"), QString(), tr("Sound files (*.wav *.mp3 *.ogg)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_document->importAudio(path, error)) EditorMessageBox::critical(this, tr("Cannot Load Sound"), error);
}
bool SoundPropertiesWindow::save()
{
    if (!m_sampleRate->lineEdit()->hasAcceptableInput()) { EditorMessageBox::warning(this, tr("Sample Rate"), tr("Enter a sample rate between 1 and 192000 Hz.")); return false; }
    applySettings(); QString error;
    if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Sound"), error); return false;
}
void SoundPropertiesWindow::closeEvent(QCloseEvent *event)
{
    m_preview->stop(); applySettings();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Sound Properties"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}

void SoundPropertiesWindow::setAudioGroups(const QStringList &groups)
{
    const QSignalBlocker blocker(m_audioGroup); const int group = m_audioGroup->currentData().toInt();
    m_audioGroup->clear();
    for (int i = 0; i < groups.size(); ++i) m_audioGroup->addItem(groups.at(i), i);
    if (m_audioGroup->findData(group) < 0) m_audioGroup->addItem(QString::number(group), group);
    m_audioGroup->setCurrentIndex(m_audioGroup->findData(group));
}
