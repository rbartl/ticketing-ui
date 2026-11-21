// Privacy policy served by the broker at /privacy (and /datenschutz).
// Contact data comes from wrangler.toml [vars]: VENDOR_NAME, CONTACT_EMAIL, CONTACT_ADDRESS, IMPRINT_URL.

const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

export function privacyPage(env) {
  const vendor = esc(env.VENDOR_NAME || "EDV Bartl");
  const email = esc(env.CONTACT_EMAIL || "");
  const address = esc(env.CONTACT_ADDRESS || "").replace(/\n/g, "<br>");
  const imprint = env.IMPRINT_URL ? `<a href="${esc(env.IMPRINT_URL)}">${esc(env.IMPRINT_URL)}</a>` : "";
  const mail = email ? `<a href="mailto:${email}">${email}</a>` : "";

  const body = `
<h1>Datenschutzerklärung – Jira Client</h1>
<p><small>Stand: ${esc(env.PRIVACY_DATE || "2026-11-21")} · <a href="#en">English version below</a></small></p>

<h2>1. Verantwortlicher</h2>
<p>${vendor}<br>${address}<br>E-Mail: ${mail}${imprint ? `<br>Impressum: ${imprint}` : ""}</p>

<h2>2. Worum es geht</h2>
<p>„Jira Client“ ist eine Desktop-Anwendung für Atlassian Jira Cloud. Sie besteht aus</p>
<ul>
  <li>der <b>Desktop-App</b> auf dem Rechner der Nutzerin bzw. des Nutzers und</li>
  <li>dem <b>Anmeldedienst</b> unter <code>jira-oauth.edv-bartl.at</code>, der ausschließlich die
      OAuth-2.0-Anmeldung bei Atlassian vermittelt.</li>
</ul>

<h2>3. Desktop-App</h2>
<ul>
  <li>Die App ruft Jira-Daten (z. B. Vorgänge, Sprints, Kommentare, Namen und E-Mail-Adressen von Beteiligten)
      <b>direkt</b> von den Atlassian-Servern ab und speichert sie als Cache <b>nur lokal</b> auf dem Gerät.</li>
  <li>Zugangsdaten (OAuth-Tokens bzw. API-Tokens) werden im Schlüsselbund des Betriebssystems gespeichert.</li>
  <li>Es werden <b>keine</b> Jira-Inhalte an ${vendor} oder sonstige Dritte übermittelt. Es gibt keine
      Analyse-, Tracking- oder Werbefunktionen.</li>
  <li>Die lokalen Daten bleiben, bis sie in der App bzw. durch Löschen des App-Datenverzeichnisses entfernt werden.
      Nicht mehr aufgerufene Vorgänge außerhalb des Sync-Bereichs werden nach 30 Tagen automatisch gelöscht.</li>
</ul>

<h2>4. Anmeldedienst (jira-oauth.edv-bartl.at)</h2>
<ul>
  <li>Bei der Anmeldung leitet der Dienst den Browser zu Atlassian weiter und tauscht den von Atlassian erhaltenen
      Autorisierungscode gegen Zugangs-Tokens. Beim Erneuern abgelaufener Zugangs-Tokens wird das Refresh-Token
      ebenfalls über diesen Dienst an Atlassian weitergereicht.</li>
  <li>Dabei werden Autorisierungscodes und Tokens <b>nur kurzzeitig im Arbeitsspeicher verarbeitet und nicht
      gespeichert</b>. Der Dienst hat keine Datenbank und protokolliert keine Tokens.</li>
  <li>Technisch notwendig verarbeitet werden außerdem IP-Adresse, Zeitpunkt und angefragte Adresse.</li>
  <li>Der Dienst wird bei <b>Cloudflare, Inc.</b> (101 Townsend St, San Francisco, CA 94107, USA) als
      Auftragsverarbeiter betrieben. Cloudflare ist nach dem EU-US Data Privacy Framework zertifiziert.
      Siehe <a href="https://www.cloudflare.com/privacypolicy/">cloudflare.com/privacypolicy</a>.</li>
  <li>Es werden keine Cookies gesetzt.</li>
</ul>

<h2>5. Atlassian</h2>
<p>Die Anmeldung selbst sowie alle Jira-Daten werden von Atlassian verarbeitet. Dafür gilt die
<a href="https://www.atlassian.com/legal/privacy-policy">Datenschutzrichtlinie von Atlassian</a>.
Freigaben für die App können jederzeit unter
<a href="https://id.atlassian.com/manage-profile/apps">id.atlassian.com → Verbundene Apps</a> widerrufen werden.</p>

<h2>6. Rechtsgrundlage</h2>
<p>Art. 6 Abs. 1 lit. b DSGVO (Bereitstellung der von der Nutzerin bzw. dem Nutzer angeforderten Anmeldung) und
Art. 6 Abs. 1 lit. f DSGVO (sicherer, technisch notwendiger Betrieb des Anmeldedienstes).</p>

<h2>7. Ihre Rechte</h2>
<p>Sie haben das Recht auf Auskunft, Berichtigung, Löschung, Einschränkung der Verarbeitung, Datenübertragbarkeit
und Widerspruch. Wenden Sie sich dazu an ${mail}. Außerdem können Sie Beschwerde bei der
Österreichischen Datenschutzbehörde (<a href="https://www.dsb.gv.at">dsb.gv.at</a>) einlegen.
Da der Anmeldedienst keine Daten speichert, liegen bei ${vendor} in der Regel keine personenbezogenen Daten
über Sie vor.</p>

<hr id="en">

<h1>Privacy Policy – Jira Client</h1>

<h2>1. Controller</h2>
<p>${vendor}<br>${address}<br>Email: ${mail}${imprint ? `<br>Imprint: ${imprint}` : ""}</p>

<h2>2. Scope</h2>
<p>"Jira Client" is a desktop application for Atlassian Jira Cloud. It consists of the <b>desktop app</b> on the
user's computer and the <b>sign-in service</b> at <code>jira-oauth.edv-bartl.at</code>, which only brokers the
OAuth 2.0 sign-in with Atlassian.</p>

<h2>3. Desktop app</h2>
<ul>
  <li>The app retrieves Jira data (e.g. issues, sprints, comments, names and email addresses of participants)
      <b>directly</b> from Atlassian and caches it <b>only locally</b> on the device.</li>
  <li>Credentials (OAuth or API tokens) are stored in the operating system's keychain.</li>
  <li><b>No</b> Jira content is transmitted to ${vendor} or any other third party. There is no analytics,
      tracking or advertising.</li>
  <li>Local data is kept until it is removed in the app or by deleting the app's data directory. Issues outside
      the sync scope that have not been opened for 30 days are deleted automatically.</li>
</ul>

<h2>4. Sign-in service</h2>
<ul>
  <li>During sign-in the service redirects the browser to Atlassian and exchanges the authorization code for
      access tokens. When access tokens expire, the refresh token is passed to Atlassian through this service.</li>
  <li>Authorization codes and tokens are <b>processed in memory only and are not stored</b>. The service has no
      database and does not log tokens.</li>
  <li>IP address, time and requested URL are processed as technically necessary.</li>
  <li>The service is hosted by <b>Cloudflare, Inc.</b> (101 Townsend St, San Francisco, CA 94107, USA) as a
      processor, certified under the EU-US Data Privacy Framework.
      See <a href="https://www.cloudflare.com/privacypolicy/">cloudflare.com/privacypolicy</a>.</li>
  <li>No cookies are used.</li>
</ul>

<h2>5. Atlassian</h2>
<p>Sign-in and all Jira data are processed by Atlassian under the
<a href="https://www.atlassian.com/legal/privacy-policy">Atlassian Privacy Policy</a>. Access granted to the app can
be revoked at any time at <a href="https://id.atlassian.com/manage-profile/apps">id.atlassian.com → Connected apps</a>.</p>

<h2>6. Legal basis</h2>
<p>Art. 6(1)(b) GDPR (providing the sign-in requested by the user) and Art. 6(1)(f) GDPR (secure, technically
necessary operation of the sign-in service).</p>

<h2>7. Your rights</h2>
<p>You have the right of access, rectification, erasure, restriction, data portability and objection; contact
${mail}. You may also lodge a complaint with the Austrian Data Protection Authority
(<a href="https://www.dsb.gv.at">dsb.gv.at</a>). As the sign-in service stores no data, ${vendor} usually holds no
personal data about you.</p>
`;

  return new Response(
    `<!doctype html><html lang="de"><meta charset="utf-8">` +
      `<meta name="viewport" content="width=device-width,initial-scale=1">` +
      `<title>Datenschutz / Privacy – Jira Client</title>` +
      `<style>body{font-family:system-ui,sans-serif;max-width:46rem;margin:2rem auto;padding:0 1rem;line-height:1.55;color:#222}` +
      `h1{font-size:1.6rem}h2{font-size:1.15rem;margin-top:1.6rem}code{background:#f2f2f2;padding:0 .2em}hr{margin:3rem 0}</style>` +
      body + `</html>`,
    {
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'",
        "Cache-Control": "public, max-age=3600",
      },
    },
  );
}
