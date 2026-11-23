# Ticketing UI

Schneller Desktop-Client für Jira Cloud (Qt 6 / C++), lokal zuerst.

## Architektur

```
Jira Cloud ──(SyncEngine, Hintergrund)──► SQLite + FTS5 (pro Konto) ──► UI
     ▲                                                                │
     └──────── Cloud-Fallback (Suche, fremde Issues, Kommentare) ◄─────┘
```

- **Sync-Bereich** pro Konto: frei wählbares JQL mit Vorlagen (leer = alle Tickets).
  Erstsync komplett, danach alle 60 s nur `updated >= -Nm`.
  Alle 6 h ID-Abgleich (gelöschte Issues / aus dem Bereich gefallene → Cache).
  Alle 5 min Sprint-Status (geschlossene Sprints).
- **Cache**: Issues außerhalb des Bereichs, die geöffnet wurden. Werden nach 30 Tagen ohne Aufruf gelöscht.
- **Kommentare** werden nicht synchronisiert: Sie werden aus dem lokalen Cache sofort angezeigt und beim Öffnen
  im Hintergrund aktualisiert (frühestens alle 30 s je Issue).
- **Suche**: Lokal per FTS5 (Key, Titel, Beschreibung, Labels) bei jedem Tastendruck. Nach 350 ms Pause zusätzlich
  `text ~ "…" AND NOT (<Bereich>)` in der Cloud. Die Treffer werden mit ☁ markiert angehängt.
- **Mehrere Konten**: eigene DB je Konto, alle synchronisieren parallel. Anmeldung per API-Token oder OAuth, Geheimnisse in `secrets.json` im Datenverzeichnis (nur für den Benutzer lesbar; kein OS-Schlüsselbund, der auf macOS nach jedem Neubau nach dem Passwort fragt).

| Datei | Inhalt |
|---|---|
| `src/Backend.h` | Schnittstelle zum Ticketsystem: Sync, Detailfelder, Aktionen, Abfrage-Vorlagen. UI, Sync und DB kennen nur diese |
| `src/JiraBackend.*` | Jira Cloud: JQL, JSON → neutrale Datensätze, Sprints, Kommentare, Statusübergänge |
| `src/JiraClient.*` | REST-Client (HTTP/2, Retry bei 429/503, Token-Erneuerung bei 401) |
| `src/Auth.*` | API-Token, OAuth-Sessions je Atlassian-Benutzer, Browser-Login (Broker oder direkt) |
| `broker/` | Cloudflare Worker: hält das OAuth-Client-Secret |
| `src/Database.*` | Schema, Upsert, FTS, Sprints, Kommentar-Cache |
| `src/SyncEngine.*` | Erstsync, inkrementeller Sync, Abgleich |
| `src/AccountContext.*` | Konto = Client + DB + Sync, Cloud-Fallback |
| `src/Adf.*` | Atlassian Document Format → HTML / Text |
| `src/MainWindow.*` | UI: Suche, Sprint, Detail |

## Anmeldung

Pro Konto (= Jira-Site) wählbar, beliebig gemischt (Menü „+ Konto“, öffnet sich im Hauptfenster):

1. **API-Token** (Standard): E-Mail + Token von [id.atlassian.com](https://id.atlassian.com/manage-profile/security/api-tokens).
   Funktioniert ohne Freigabe durch die Firma.
2. **Mit Atlassian anmelden (OAuth 2.0)**: Der Browser öffnet die Atlassian-Anmeldung, die App empfängt die
   Antwort auf `http://localhost:<port>/callback` (mit Broker ein freier Port, ohne Broker 47823). Die Anmeldung
   bleibt im Hauptfenster offen, bis sie fertig ist; der Link lässt sich kopieren, um ihn in einem anderen Browser zu öffnen.
   Setzt voraus, dass die jeweilige Firma die OAuth-App zulässt.
   - **Mehrere Identitäten**: eigene Adresse und Kunden-Adressen nebeneinander. Je Atlassian-Benutzer eine
     Session (Refresh-Token rotierend, in `secrets.json` unter `oauth:<accountId>`), geteilt von allen Sites
     dieses Benutzers. Für ein anderes Konto im Browser das Atlassian-Konto wechseln oder ein privates Fenster nutzen.
   - **Weitere Site desselben Benutzers**: erneut anmelden und auf der Zustimmungsseite die andere Site wählen.
   - API-Aufrufe laufen über `https://api.atlassian.com/ex/jira/<cloudId>`.

### OAuth-App einmalig registrieren

1. developer.atlassian.com → *Developer console* → *Create* → **OAuth 2.0 integration**
2. *Permissions*:
   - **Jira API**: `read:jira-work`, `write:jira-work`, `read:jira-user`, granular `read:sprint:jira-software`
   - **User identity API**: `read:me` (damit die App weiß, *wer* sich angemeldet hat)
3. *Authorization* → OAuth 2.0 (3LO) → Callback-URL:
   - mit Broker: `https://jira-oauth.edv-bartl.at/callback`
   - ohne Broker: `http://localhost:47823/callback`
4. *Distribution* → **Sharing** aktivieren, wenn außer dir noch jemand die App nutzen soll.

Die Scopes müssen überall gleich sein: Console, `SCOPES` in `broker/worker.js`, `OAuthConfig::scopes()` in `src/Auth.cpp`.

### Broker (Cloudflare Worker, empfohlen)

Atlassian Cloud verlangt für OAuth ein Client-Secret (keine Public Clients / kein PKCE ohne Secret).
Der Worker in `broker/` hält das Secret, damit es nicht in der App steckt. Er speichert nichts. Der Zustand
reist HMAC-signiert im `state`, und per PKCE kann nur die App, die den Login gestartet hat, den Code einlösen.

```sh
cd broker
npm test                                   # Offline-Test (Node ≥ 18)
# ATLASSIAN_CLIENT_ID in wrangler.toml eintragen, dann:
npx wrangler login
npx wrangler secret put ATLASSIAN_CLIENT_SECRET
openssl rand -base64 48 | npx wrangler secret put STATE_KEY
npx wrangler deploy                        # -> https://jira-oauth.edv-bartl.at (DNS + Zertifikat automatisch)
```

| Endpoint | Zweck |
|---|---|
| `GET /login?port&state&challenge` | Start → Weiterleitung zu Atlassian |
| `GET /callback` | Atlassian → zurück zur App auf `localhost:<port>` |
| `POST /token {code,state,verifier}` | Code → Tokens (mit Secret) |
| `POST /refresh {refresh_token}` | Token erneuern (mit Secret) |
| `GET /privacy`, `/datenschutz` | Datenschutzerklärung (für *Distribution → Sharing*), Kontaktdaten in `wrangler.toml` |

### App konfigurieren

Standardmäßig nutzt die App den Broker `https://jira-oauth.edv-bartl.at` (CMake-Option `JIRA_OAUTH_BROKER_URL`),
es ist also nichts zu konfigurieren. Überschreiben lässt sich das per `<AppData>/oauth.json` (Linux: `~/.local/share/edvbartl/ticketing-ui/oauth.json`), oder dieselben Werte als
CMake-Optionen (`-DJIRA_OAUTH_BROKER_URL=…` bzw. `-DJIRA_OAUTH_CLIENT_ID=… -DJIRA_OAUTH_CLIENT_SECRET=…`):

```json
{ "broker_url": "https://jira-oauth.edv-bartl.at" }
```
oder ohne Broker (eigene App, Secret lokal):
```json
{ "client_id": "…", "client_secret": "…", "port": 47823 }
```

## Tastatur

| Taste | Aktion |
|---|---|
| `Strg+L` / `Strg+F` | Suchfeld |
| `Enter` (im Suchfeld) | in die Ergebnisliste |
| `Esc` | Suche leeren / zurück zur Suche |
| `Strg+T` | Suche ↔ Sprint |
| `Strg+1…9` | Konto wechseln |
| `Strg+R` | sofort synchronisieren |

## Bauen

Benötigt: Qt ≥ 6.4 (Widgets, Network, Sql + SQLite-Treiber), CMake ≥ 3.21.

**Debian/Ubuntu**
```sh
sudo apt install cmake ninja-build qt6-base-dev libqt6sql6-sqlite
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/ticketing-ui
```

**macOS**
```sh
brew install qt cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build && macdeployqt build/ticketing-ui.app
```

**Windows**: Qt 6 über den Qt Online Installer (MSVC),
dann mit CMake bauen und `windeployqt` ausführen.

Daten liegen unter dem `AppDataLocation` von Qt (Linux: `~/.local/share/edvbartl/ticketing-ui/`).
