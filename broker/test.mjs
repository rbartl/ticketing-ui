// Offline test of the broker: node test.mjs
import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import worker from "./worker.js";

const env = { ATLASSIAN_CLIENT_ID: "cid", ATLASSIAN_CLIENT_SECRET: "csecret", STATE_KEY: "k".repeat(40) };
const B = "https://broker.example.workers.dev";
const b64url = (b) => b.toString("base64url");

// Fake Atlassian token endpoint
const seen = [];
globalThis.fetch = async (url, init) => {
  const body = JSON.parse(init.body);
  seen.push(body);
  assert.equal(url, "https://auth.atlassian.com/oauth/token");
  assert.equal(body.client_secret, "csecret");
  if (body.grant_type === "authorization_code" && body.code === "good")
    return new Response(JSON.stringify({ access_token: "AT", refresh_token: "RT", expires_in: 3600 }));
  if (body.grant_type === "refresh_token" && body.refresh_token === "RT")
    return new Response(JSON.stringify({ access_token: "AT2", refresh_token: "RT2", expires_in: 3600 }));
  return new Response(JSON.stringify({ error: "invalid_grant" }), { status: 403 });
};
const call = (path, init) => worker.fetch(new Request(B + path, init), env);
const post = (path, obj) => call(path, { method: "POST", body: JSON.stringify(obj) });

const verifier = b64url(randomBytes(32));
const challenge = b64url(createHash("sha256").update(verifier).digest());

// 1. /login -> Atlassian authorize with signed state + broker callback
let r = await call(`/login?port=47823&state=appstate1&challenge=${challenge}`);
assert.equal(r.status, 302);
const auth = new URL(r.headers.get("Location"));
assert.equal(auth.origin + auth.pathname, "https://auth.atlassian.com/authorize");
assert.equal(auth.searchParams.get("redirect_uri"), B + "/callback");
assert.equal(auth.searchParams.get("client_id"), "cid");
assert.ok(auth.searchParams.get("scope").includes("offline_access"));
assert.ok(!r.headers.get("Location").includes("csecret"));
const state = auth.searchParams.get("state");

// 2. /callback -> only to localhost, carries app_state + code
r = await call(`/callback?code=good&state=${encodeURIComponent(state)}`);
assert.equal(r.status, 302);
const back = new URL(r.headers.get("Location"));
assert.equal(back.origin, "http://localhost:47823");
assert.equal(back.searchParams.get("app_state"), "appstate1");
assert.equal(back.searchParams.get("code"), "good");

// 3. /token needs the right verifier
r = await post("/token", { code: "good", state, verifier: b64url(randomBytes(32)) });
assert.equal(r.status, 400, "wrong verifier must fail");
r = await post("/token", { code: "good", state, verifier });
assert.equal(r.status, 200);
assert.deepEqual(await r.json(), { access_token: "AT", refresh_token: "RT", expires_in: 3600 });
assert.equal(seen.at(-1).redirect_uri, B + "/callback");

// 4. /refresh passes through, errors keep Atlassian's status
r = await post("/refresh", { refresh_token: "RT" });
assert.equal((await r.json()).access_token, "AT2");
r = await post("/refresh", { refresh_token: "revoked" });
assert.equal(r.status, 403);

// 5. tampered / foreign state is rejected (no redirect to attacker-chosen port)
const [body, mac] = state.split(".");
const evil = Buffer.from(JSON.stringify({ ...JSON.parse(Buffer.from(body, "base64url")), p: 8080 })).toString("base64url");
r = await call(`/callback?code=x&state=${evil}.${mac}`);
assert.equal(r.status, 400);
r = await post("/token", { code: "good", state: `${evil}.${mac}`, verifier });
assert.equal(r.status, 400);

// 6. expired state
const realNow = Date.now;
Date.now = () => realNow() + 11 * 60 * 1000;
r = await post("/token", { code: "good", state, verifier });
assert.equal(r.status, 400, "expired state must fail");
Date.now = realNow;

// 7. input validation
assert.equal((await call(`/login?port=80&state=a&challenge=${challenge}`)).status, 400);
assert.equal((await call(`/login?port=47823&state=a&challenge=short`)).status, 400);
assert.equal((await call(`/login?port=47823&state=a<b&challenge=${challenge}`)).status, 400);
assert.equal((await call("/nope")).status, 404);
assert.equal((await worker.fetch(new Request(B + "/login"), {})).status, 500, "unconfigured");

// 8. privacy page works even before OAuth is configured, escapes config values
r = await worker.fetch(new Request(B + "/privacy"), { CONTACT_EMAIL: "a@b.at", CONTACT_ADDRESS: "<x>" });
assert.equal(r.status, 200);
const page = await r.text();
assert.ok(page.includes("mailto:a@b.at") && page.includes("Cloudflare") && page.includes('id="en"'));
assert.ok(!page.includes("<x>") && page.includes("&lt;x&gt;"));
assert.equal((await call("/datenschutz")).status, 200);

console.log("broker: all tests passed");
