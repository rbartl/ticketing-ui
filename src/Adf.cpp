#include "Adf.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

namespace {

QString esc(const QString &s) { return s.toHtmlEscaped(); }

void html(const QJsonObject &n, QString &out);

void htmlChildren(const QJsonObject &n, QString &out)
{
    for (const auto &c : n.value("content").toArray())
        html(c.toObject(), out);
}

QString textWithMarks(const QJsonObject &n)
{
    QString t = esc(n.value("text").toString());
    for (const auto &mv : n.value("marks").toArray()) {
        const auto m = mv.toObject();
        const auto type = m.value("type").toString();
        if (type == "strong") t = "<b>" + t + "</b>";
        else if (type == "em") t = "<i>" + t + "</i>";
        else if (type == "code") t = "<code>" + t + "</code>";
        else if (type == "strike") t = "<s>" + t + "</s>";
        else if (type == "underline") t = "<u>" + t + "</u>";
        else if (type == "link")
            t = "<a href=\"" + esc(m.value("attrs").toObject().value("href").toString()) + "\">" + t + "</a>";
    }
    return t;
}

struct Wrap { const char *type; const char *open; const char *close; };
const Wrap kWraps[] = {
    {"doc", "", ""},
    {"paragraph", "<p>", "</p>"},
    {"bulletList", "<ul>", "</ul>"},
    {"orderedList", "<ol>", "</ol>"},
    {"listItem", "<li>", "</li>"},
    {"taskList", "<ul>", "</ul>"},
    {"taskItem", "<li>", "</li>"},
    {"decisionList", "<ul>", "</ul>"},
    {"decisionItem", "<li>", "</li>"},
    {"blockquote", "<blockquote>", "</blockquote>"},
    {"table", "<table border='1' cellspacing='0' cellpadding='4'>", "</table>"},
    {"tableRow", "<tr>", "</tr>"},
    {"tableHeader", "<th>", "</th>"},
    {"tableCell", "<td>", "</td>"},
    {"panel", "<table width='100%' cellpadding='6'><tr><td bgcolor='#e9eef8'>", "</td></tr></table>"},
};

void html(const QJsonObject &n, QString &out)
{
    const QString type = n.value("type").toString();
    const QJsonObject a = n.value("attrs").toObject();

    if (type == "text") { out += textWithMarks(n); return; }
    if (type == "hardBreak") { out += "<br>"; return; }
    if (type == "rule") { out += "<hr>"; return; }
    if (type == "mention") { out += "<b>" + esc(a.value("text").toString()) + "</b>"; return; }
    if (type == "emoji") {
        const auto t = a.value("text").toString();
        out += esc(t.isEmpty() ? a.value("shortName").toString() : t);
        return;
    }
    if (type == "status") { out += "<code>[" + esc(a.value("text").toString()) + "]</code>"; return; }
    if (type == "date") {
        const auto ms = a.value("timestamp").toString().toLongLong();
        out += QDateTime::fromMSecsSinceEpoch(ms).date().toString("dd.MM.yyyy");
        return;
    }
    if (type == "inlineCard" || type == "blockCard" || type == "embedCard") {
        const auto u = esc(a.value("url").toString());
        out += "<a href=\"" + u + "\">" + u + "</a>";
        return;
    }
    if (type.startsWith("media")) { out += "<i>[Anhang]</i>"; return; }
    if (type == "heading") {
        const int level = qBound(1, a.value("level").toInt(1) + 2, 6);  // h1 is used for the issue title
        out += QString("<h%1>").arg(level);
        htmlChildren(n, out);
        out += QString("</h%1>").arg(level);
        return;
    }
    if (type == "codeBlock") {
        out += "<pre>";
        for (const auto &c : n.value("content").toArray())
            out += esc(c.toObject().value("text").toString());
        out += "</pre>";
        return;
    }
    if (type == "expand" || type == "nestedExpand") {
        out += "<p><b>" + esc(a.value("title").toString()) + "</b></p>";
        htmlChildren(n, out);
        return;
    }
    for (const auto &w : kWraps) {
        if (type == QLatin1String(w.type)) {
            out += QLatin1String(w.open);
            htmlChildren(n, out);
            out += QLatin1String(w.close);
            return;
        }
    }
    htmlChildren(n, out);  // unknown node: render its content
}

void text(const QJsonObject &n, QString &out)
{
    const QString type = n.value("type").toString();
    const QJsonObject a = n.value("attrs").toObject();
    if (type == "text") { out += n.value("text").toString(); return; }
    if (type == "hardBreak") { out += '\n'; return; }
    if (type == "mention" || type == "emoji" || type == "status") { out += a.value("text").toString(); return; }
    if (type == "inlineCard") { out += a.value("url").toString(); return; }
    for (const auto &c : n.value("content").toArray())
        text(c.toObject(), out);
    if (type != "doc")
        out += '\n';
}

} // namespace

QString Adf::toHtml(const QJsonValue &doc)
{
    if (doc.isString())
        return esc(doc.toString()).replace('\n', "<br>");
    QString out;
    if (doc.isObject())
        html(doc.toObject(), out);
    return out;
}

QString Adf::toText(const QJsonValue &doc)
{
    if (doc.isString())
        return doc.toString();
    QString out;
    if (doc.isObject())
        text(doc.toObject(), out);
    return out;
}
