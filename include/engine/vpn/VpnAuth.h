#pragma once

#include <QList>
#include <QPair>
#include <QString>

// Plain mirror of libcore::VPNChallengeField, so the proto headers stay out of moc's way.
struct VpnAuthField {
    QString submissionKey;
    QString name;
    QString label;
    QString kind;
    QString value;
    QList<QPair<QString, QString>> options;
};

struct VpnAuthChallenge {
    QString endpointTag;
    QString id;
    QString kind;
    QString username;
    QString message;
    QString banner;
    QString error;
    QString url;
    bool echo = false;
    qint64 deadline = 0;
    QList<VpnAuthField> fields;
};
