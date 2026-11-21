#pragma once
#include <QJsonValue>
#include <QString>

// Atlassian Document Format (Jira Cloud rich text) -> HTML / plain text.
namespace Adf {
QString toHtml(const QJsonValue &doc);
QString toText(const QJsonValue &doc);
}
