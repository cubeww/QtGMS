#include "mainwindow.h"
#include "editorstandarddialogs.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMenu>
#include <QSettings>

static const int MaxRecentProjects = 10;
static const QString RecentProjectsKey = QStringLiteral("projects/recent");

void MainWindow::rememberProject()
{
    if (!m_project.isOpen() || m_project.isTemporary()) return;
    const QString path = QDir::cleanPath(QFileInfo(m_project.filePath()).absoluteFilePath());
    QSettings settings;
    const QStringList previous = settings.value(RecentProjectsKey).toStringList();
    QStringList recent;
    recent.append(path);
    for (const QString &entry : previous) {
        if (recent.size() >= MaxRecentProjects) break;
        if (!recent.contains(entry, Qt::CaseInsensitive)) recent.append(entry);
    }
    settings.setValue(RecentProjectsKey, recent);
}

void MainWindow::refreshRecentProjects()
{
    m_recentProjectsMenu->clear();
    const QStringList recent = QSettings().value(RecentProjectsKey).toStringList().mid(0, MaxRecentProjects);
    if (recent.isEmpty()) {
        m_recentProjectsMenu->addAction(tr("No recent projects"))->setEnabled(false);
        return;
    }
    const QFontMetrics metrics(m_recentProjectsMenu->font());
    for (int index = 0; index < recent.size(); ++index) {
        const QString path = recent.at(index), displayPath = QDir::toNativeSeparators(path);
        QString caption = metrics.elidedText(displayPath, Qt::ElideMiddle, 600);
        caption.replace(QLatin1Char('&'), QStringLiteral("&&"));
        caption.prepend(index < 9 ? QStringLiteral("&%1 ").arg(index + 1) : QStringLiteral("1&0 "));
        QAction *action = m_recentProjectsMenu->addAction(QIcon(QStringLiteral(":/images/open.png")), caption);
        action->setData(path);
        action->setToolTip(displayPath);
        action->setEnabled(!m_building && !m_treeBusy);
        connect(action, &QAction::triggered, this, [this, path] {
            if (m_building || m_treeBusy) return;
            if (!QFileInfo(path).isFile()) {
                QSettings settings;
                QStringList paths = settings.value(RecentProjectsKey).toStringList();
                for (int i = paths.size() - 1; i >= 0; --i)
                    if (paths.at(i).compare(path, Qt::CaseInsensitive) == 0) paths.removeAt(i);
                settings.setValue(RecentProjectsKey, paths);
                EditorMessageBox::warning(this, tr("Cannot Open Project"),
                    tr("The project file no longer exists and has been removed from Recent Projects:\n%1")
                        .arg(QDir::toNativeSeparators(path)));
                return;
            }
            loadProject(path);
        });
    }
    m_recentProjectsMenu->addSeparator();
    QAction *clear = m_recentProjectsMenu->addAction(tr("Clear Recent Projects"));
    connect(clear, &QAction::triggered, this, [] { QSettings().remove(RecentProjectsKey); });
}
