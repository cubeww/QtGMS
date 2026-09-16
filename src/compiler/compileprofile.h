#ifndef QTGMS_COMPILEPROFILE_H
#define QTGMS_COMPILEPROFILE_H

#include <QElapsedTimer>
#include <QString>
#include <QVector>

struct CompileDetailTiming
{
    QString name;
    qint64 nanoseconds = 0;
};

struct CompileCounter
{
    QString name;
    int count = 0;
};

struct CompileStageTiming
{
    QString name;
    qint64 nanoseconds = 0;
    QVector<CompileDetailTiming> details;
    QVector<CompileCounter> counters;
};

// Sequential stages are exclusive: starting one finishes the previous stage.
class CompileProfile
{
public:
    explicit CompileProfile(QVector<CompileStageTiming> &stages)
        : m_stages(stages)
    {
    }

    void start(const QString &name)
    {
        finish();
        m_current = CompileStageTiming();
        m_current.name = name;
        m_timer.start();
    }

    void finish()
    {
        if (!m_timer.isValid())
            return;
        m_current.nanoseconds = m_timer.nsecsElapsed();
        m_stages.append(m_current);
        m_timer.invalidate();
    }

    int detailIndex(const QString &name)
    {
        for (int i = 0; i < m_current.details.size(); ++i)
            if (m_current.details.at(i).name == name)
                return i;
        CompileDetailTiming detail;
        detail.name = name;
        m_current.details.append(detail);
        return m_current.details.size() - 1;
    }

    void addDetail(int index, qint64 nanoseconds) { m_current.details[index].nanoseconds += nanoseconds; }
    void addDetail(const QString &name, qint64 nanoseconds) { addDetail(detailIndex(name), nanoseconds); }

    void count(const QString &name, int amount = 1)
    {
        for (auto &counter : m_current.counters)
            if (counter.name == name) {
                counter.count += amount;
                return;
            }
        CompileCounter counter;
        counter.name = name;
        counter.count = amount;
        m_current.counters.append(counter);
    }

private:
    QVector<CompileStageTiming> &m_stages;
    QElapsedTimer m_timer;
    CompileStageTiming m_current;
};

// Detail scopes stay within one stage and must not overlap other detail scopes.
class CompileDetailScope
{
public:
    CompileDetailScope(CompileProfile &profile, const QString &name)
        : m_profile(profile)
        , m_index(profile.detailIndex(name))
    {
        m_timer.start();
    }
    ~CompileDetailScope() { m_profile.addDetail(m_index, m_timer.nsecsElapsed()); }
    CompileDetailScope(const CompileDetailScope &) = delete;
    CompileDetailScope &operator=(const CompileDetailScope &) = delete;

private:
    CompileProfile &m_profile;
    int m_index;
    QElapsedTimer m_timer;
};

#endif
