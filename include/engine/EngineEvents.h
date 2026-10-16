#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace Engine {
    // Engine-originated half of the old MwMessage bus; the instance lives on the main thread, emitting is safe from any thread.
    class EngineEvents : public QObject {
        Q_OBJECT

    public:
        static EngineEvents *instance();

        static void SubscriptionNewGroup();
        static void SubscriptionFinished(int importedCount, bool quiet);
        static void SubscriptionGroupChanged(int groupId, const QList<int> &disturbedProfileIds);
        static void SubscriptionDiff(const QString &title, const QString &body);
        static void GroupOrderChanged(const QList<int> &groupIds);
        static void ExtraCoreExited();

    signals:
        void subscriptionNewGroup();
        // importedCount < 0 when the job did not count.
        void subscriptionFinished(int importedCount, bool quiet);
        void subscriptionGroupChanged(int groupId, const QList<int> &disturbedProfileIds);
        void subscriptionDiff(const QString &title, const QString &body);
        void groupOrderChanged(const QList<int> &groupIds);
        void extraCoreExited();

    private:
        explicit EngineEvents(QObject *parent = nullptr);
    };
} // namespace Engine
