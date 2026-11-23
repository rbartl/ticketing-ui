// OAuth broker for ticketing-ui (Cloudflare Worker).
//
// Atlassian Cloud OAuth 2.0 (3LO) only supports confidential clients, i.e. the token
// exchange needs the client secret. This worker holds the secret so the desktop app
// does not have to. It stores nothing: all state travels in an HMAC-signed `state`.
//
//   app ──/login?port&state&challenge──► worker ──302──► auth.atlassian.com
//   auth.atlassian.com ──/callback?code&state──► worker ──302──► http://localhost:<port>/callback
//   app ──POST /token {code,state,verifier}──► worker ──(+secret)──► auth.atlassian.com
//   app ──POST /refresh {refresh_token}─────► worker ──(+secret)──► auth.atlassian.com
//
// PKCE between app and worker: only the app that started the login (and knows the
// verifier) can redeem the code, even if the redirect URL leaks.
//
// Config:  vars:    ATLASSIAN_CLIENT_ID
//          secrets: ATLASSIAN_CLIENT_SECRET, STATE_KEY (random, >= 32 chars)

import { privacyPage } from "./privacy.js";

const AUTHORIZE_URL = "https://auth.atlassian.com/authorize";
const TOKEN_URL = "https://auth.atlassian.com/oauth/token";
const SCOPES = "read:jira-work write:jira-work read:jira-user read:sprint:jira-software read:me offline_access";
const STATE_TTL_SECONDS = 10 * 60;
const B64URL = /^[A-Za-z0-9_-]+$/;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    // HEAD too: link checkers (e.g. the developer console's privacy URL check) probe with HEAD first.
    const path = url.pathname.replace(/\/+$/, "");
    if ((request.method === "GET" || request.method === "HEAD") && (path === "/privacy" || path === "/datenschutz")) {
      const page = privacyPage(env);
      return request.method === "HEAD" ? new Response(null, page) : page;
    }
    try {
      if (!env.ATLASSIAN_CLIENT_ID || !env.ATLASSIAN_CLIENT_SECRET || !env.STATE_KEY)
        return json({ error: "server_error", error_description: "broker not configured" }, 500);
      if (request.method === "GET" && url.pathname === "/login") return await login(url, env);
      if (request.method === "GET" && url.pathname === "/callback") return await callback(url, env);
      if (request.method === "POST" && url.pathname === "/token") return await token(request, url, env);
      if (request.method === "POST" && url.pathname === "/refresh") return await refresh(request, env);
      if (request.method === "GET" && url.pathname === "/") return new Response("ticketing-ui oauth broker\n");
      return json({ error: "not_found" }, 404);
    } catch (e) {
      return json({ error: "invalid_request", error_description: String(e?.message ?? e) }, 400);
    }
  },
};

// GET /login?port=47823&state=<app state>&challenge=<base64url(sha256(verifier))>
async function login(url, env) {
  const port = Number(url.searchParams.get("port"));
  const appState = url.searchParams.get("state") ?? "";
  const challenge = url.searchParams.get("challenge") ?? "";
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error("invalid port");
  if (!B64URL.test(appState) || appState.length > 128) throw new Error("invalid state");
  if (!B64URL.test(challenge) || challenge.length !== 43) throw new Error("invalid challenge");

  const state = await sign(env, {
    p: port,
    s: appState,
    c: challenge,
    e: Math.floor(Date.now() / 1000) + STATE_TTL_SECONDS,
  });
  const target = new URL(AUTHORIZE_URL);
  target.search = new URLSearchParams({
    audience: "api.atlassian.com",
    client_id: env.ATLASSIAN_CLIENT_ID,
    scope: SCOPES,
    redirect_uri: redirectUri(url, env),
    state,
    response_type: "code",
    prompt: "consent",
  }).toString();
  return redirect(target.toString());
}

// GET /callback?code=…&state=…   (from Atlassian) -> back to the app on localhost
async function callback(url, env) {
  const payload = await verify(env, url.searchParams.get("state") ?? "");
  if (!payload) return html("Ungültige oder abgelaufene Anmeldung. Bitte in der App erneut starten.", 400);

  // Only ever redirect to the loopback interface -> no open redirect.
  const target = new URL(`http://localhost:${payload.p}/callback`);
  const q = new URLSearchParams({ app_state: payload.s, state: url.searchParams.get("state") });
  const error = url.searchParams.get("error");
  if (error) {
    q.set("error", error);
    q.set("error_description", url.searchParams.get("error_description") ?? "");
  } else {
    q.set("code", url.searchParams.get("code") ?? "");
  }
  target.search = q.toString();
  return redirect(target.toString());
}

// POST /token {code, state, verifier}
async function token(request, url, env) {
  const { code, state, verifier } = await request.json();
  const payload = await verify(env, state ?? "");
  if (!payload) return json({ error: "invalid_grant", error_description: "invalid or expired state" }, 400);
  if (typeof verifier !== "string" || verifier.length < 43 || (await sha256(verifier)) !== payload.c)
    return json({ error: "invalid_grant", error_description: "PKCE verification failed" }, 400);
  if (typeof code !== "string" || !code) throw new Error("missing code");

  return forward(env, {
    grant_type: "authorization_code",
    code,
    redirect_uri: redirectUri(url, env),
  });
}

// POST /refresh {refresh_token}
async function refresh(request, env) {
  const { refresh_token } = await request.json();
  if (typeof refresh_token !== "string" || !refresh_token) throw new Error("missing refresh_token");
  return forward(env, { grant_type: "refresh_token", refresh_token });
}

// Adds the client credentials and passes Atlassian's answer through unchanged.
async function forward(env, params) {
  const res = await fetch(TOKEN_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify({
      ...params,
      client_id: env.ATLASSIAN_CLIENT_ID,
      client_secret: env.ATLASSIAN_CLIENT_SECRET,
    }),
  });
  return new Response(await res.text(), {
    status: res.status,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}

function redirectUri(url, env) {
  return env.REDIRECT_URI || `${url.origin}/callback`;
}

// --- signed state ---------------------------------------------------------------

async function hmacKey(env) {
  return crypto.subtle.importKey("raw", new TextEncoder().encode(env.STATE_KEY),
    { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}

async function sign(env, payload) {
  const body = b64url(new TextEncoder().encode(JSON.stringify(payload)));
  const mac = await crypto.subtle.sign("HMAC", await hmacKey(env), new TextEncoder().encode(body));
  return `${body}.${b64url(new Uint8Array(mac))}`;
}

async function verify(env, state) {
  try {
    return await verifyUnsafe(env, state);
  } catch {
    return null;  // malformed base64 / JSON -> just an invalid state
  }
}

async function verifyUnsafe(env, state) {
  const [body, mac] = state.split(".");
  if (!body || !mac || !B64URL.test(body) || !B64URL.test(mac)) return null;
  const ok = await crypto.subtle.verify("HMAC", await hmacKey(env), unb64url(mac),
    new TextEncoder().encode(body));  // constant time
  if (!ok) return null;
  const payload = JSON.parse(new TextDecoder().decode(unb64url(body)));
  return payload.e >= Math.floor(Date.now() / 1000) ? payload : null;
}

async function sha256(text) {
  return b64url(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text))));
}

function b64url(bytes) {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function unb64url(s) {
  const bin = atob(s.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((s.length + 3) % 4));
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}

// --- responses ------------------------------------------------------------------

function redirect(location) {
  return new Response(null, { status: 302, headers: { Location: location, "Cache-Control": "no-store" } });
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}

function html(message, status) {
  const esc = message.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]);
  return new Response(`<!doctype html><meta charset=utf-8><title>Ticketing UI</title>` +
    `<body style="font-family:sans-serif;margin:3em"><p>${esc}</p></body>`,
    { status, headers: { "Content-Type": "text/html; charset=utf-8" } });
}
