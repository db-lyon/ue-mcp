import { createSign } from "node:crypto";
import { resolveUserAuth, clearUserAuth, type PendingDeviceFlow } from "./auth.js";

const APP_ID = "3133514";
const REPO_OWNER = "db-lyon";
const REPO_NAME = "ue-mcp";

const EMBEDDED_PEM: string = `-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEAz+75aawv43S5EUSOsSAufEEj9z0bPHM6VoR4sTXmMAoRxhpQ
yM7UIcQzxhdys1drVWcT1ZchmysRwE+NKgvXX53Yvbi7Fge1At1xHYT3VqFtmqbK
9xBRbFqeLb2bFsCku7SMcFey6E8LOlib3m0cynxT2lkvQFIawgfd3oDrtx1IGda2
Ra6aMUDTd9K/Kb7DTpigckLu/hnrZxzRtbn7qpDQuW53n49xM8/2XKsWGL6Z9psl
zMTGW2Q7ej63LSLvpWWydZTl6TWQIrUrdK5vPd01yeChnozYlbhthQmXTuV+zOeq
3WjPyrgcYLBuxpYUambf72/jH3BASha7KiBtwwIDAQABAoIBAHRBps2Aah4ASuDu
teEunw711MgNMEcyHbH1yw05l4PQfXOHjxdXHJ/sdQ4SWh0PiYFsaliHcVCyWfBu
Cf0yNa7OMDqTKHb+xCPf77iTeT7EbuWC0AQm6X9tgvcMBcRI3VHddo/xWKQuXZa3
qB5KX0iPDssMBjEuNqu5fkFDT4dHIuUWxEdeNa0Aooc2qpR08sxMissPx4/Ii/5i
JRLEfe40HyrA88nfw5lPAo/IKCkHMuOWjaY43oXLerVjUvtzthidyt9cm5QhGkbf
9oAy7oFBoLoSYY1avdW3lS3hFTPMlPPQBJMtF2xJowWYdnpcAFNUTceZcGg/HZNy
eR0/WEECgYEA++eTSKiNsbXDcXcRdpsbQT7nj2DqizxF0pIqitmqONHXPyckp0RS
3BhKqgKQdZkE6yHcM7j0Ze3XpGa0s/+OCpUTGXmdHgkNX6S3jMSJfsC8DhadWY4H
o5iLh5n+uPf8Eqygzv8YDdI6MsmDB5uTlVehXcubD+Fv74LA+dd18JECgYEA01Bk
QVikrjhnLtWyHDLsSOq1q45xHhrHtFhgNNsg34rWvBloIbAKyUZ7yJSo9Opti2A+
GwbmNhlVa8u3rHhN+IaiFG2O7zuFnD0HRAB+Q5zx999lWbNKSdHOoTdq0FUVNWeo
g4IxjT2jElPJ+/0z6gqmFtd5YrSfJfYaNk6f4xMCgYEAlWGupdfOryPq0s6ZPIye
jQKQryX58Le9cDHdqJmLqEZILts3kTmjKYH+RPNgV1x23jkmLEXyKL/ysTt2zYcu
5Hei6+iCk90qYR18+61RHCmPW4ttpD3lqc48cB2SQH5OrYRRmG8OBKQ2fweg/FH2
IwnkV4r2WWSGzLHtXju3ZQECgYBwO6qD6ojM37gt7+IBiCpnMAH9dFIwIxkeeDHg
4OG82QjkLrpX6iBQdtcX2Z1DN5+m/x79S9TJtvrfnfuX5u3Cqf87ylS4S2zTZraN
L5XgY2NAu64gzlEOlBijF8PrksUI8F1YO7YFxk7uVQJajEkebTl5uEgIXNaKWwPh
BDF3cQKBgQCi5aAK+wkUHOIK23d++641luNCBkJKgtOOMnmjiL29WEK/xkHYvgEH
sYqGb06toaVrPFEEtkPvbbXsdxbw4aGPtQi9c7s9+Jerym2PsUTthC9Cw2nJgBgn
SRNeKaBiYe12IIVfxqTb8tGVuR3VHxjQR13q5eMn4n08D47PF6VD4g==
-----END RSA PRIVATE KEY-----`;

function base64url(input: string): string {
  return Buffer.from(input).toString("base64url");
}

function createJWT(pem: string): string {
  const now = Math.floor(Date.now() / 1000);
  const header = base64url(JSON.stringify({ alg: "RS256", typ: "JWT" }));
  const payload = base64url(
    JSON.stringify({
      iss: APP_ID,
      iat: now - 60,
      exp: now + 600,
    }),
  );

  const unsigned = `${header}.${payload}`;
  const sign = createSign("RSA-SHA256");
  sign.update(unsigned);
  const signature = sign.sign(pem, "base64url");

  return `${unsigned}.${signature}`;
}

async function getInstallationToken(jwt: string): Promise<string> {
  const res = await fetch("https://api.github.com/app/installations", {
    headers: {
      Authorization: `Bearer ${jwt}`,
      Accept: "application/vnd.github+json",
    },
  });

  if (!res.ok) {
    throw new Error(`GitHub App auth failed: ${res.status}`);
  }

  const installations = (await res.json()) as Array<{ id: number }>;
  if (installations.length === 0) {
    throw new Error("GitHub App has no installations");
  }

  const tokenRes = await fetch(
    `https://api.github.com/app/installations/${installations[0].id}/access_tokens`,
    {
      method: "POST",
      headers: {
        Authorization: `Bearer ${jwt}`,
        Accept: "application/vnd.github+json",
      },
    },
  );

  if (!tokenRes.ok) {
    throw new Error(`Failed to get installation token: ${tokenRes.status}`);
  }

  return ((await tokenRes.json()) as { token: string }).token;
}

export type SubmitResult =
  | {
      kind: "submitted";
      url: string;
      number: number;
      authoredBy: string;
      authoredAs: "user" | "bot";
    }
  | {
      kind: "auth_required";
      verification_uri: string;
      user_code: string;
      expires_in: number;
    };

async function submitAsBot(
  title: string,
  body: string,
  labels: string[],
): Promise<SubmitResult> {
  const jwt = createJWT(EMBEDDED_PEM);
  const token = await getInstallationToken(jwt);
  const res = await fetch(
    `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/issues`,
    {
      method: "POST",
      headers: {
        Authorization: `token ${token}`,
        Accept: "application/vnd.github+json",
        "Content-Type": "application/json",
        "User-Agent": "ue-mcp",
      },
      body: JSON.stringify({ title, body, labels }),
    },
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Failed to create issue (bot): ${res.status} ${text}`);
  }
  const issue = (await res.json()) as { html_url: string; number: number };
  return {
    kind: "submitted",
    url: issue.html_url,
    number: issue.number,
    authoredBy: "ue-mcp-feedback[bot]",
    authoredAs: "bot",
  };
}

function pendingResult(pending: PendingDeviceFlow): SubmitResult {
  return {
    kind: "auth_required",
    verification_uri: pending.verification_uri,
    user_code: pending.user_code,
    expires_in: Math.max(0, pending.expires_at - Math.floor(Date.now() / 1000)),
  };
}

export async function submitFeedback(
  title: string,
  body: string,
  labels: string[] = ["agent-feedback"],
  options: { useBot?: boolean } = {},
): Promise<SubmitResult> {
  if (options.useBot) {
    return submitAsBot(title, body, labels);
  }

  const auth = await resolveUserAuth();
  if (auth.kind === "pending") {
    return pendingResult(auth.pending);
  }

  const res = await fetch(
    `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/issues`,
    {
      method: "POST",
      headers: {
        Authorization: `token ${auth.auth.token}`,
        Accept: "application/vnd.github+json",
        "Content-Type": "application/json",
        "User-Agent": "ue-mcp",
      },
      body: JSON.stringify({ title, body, labels }),
    },
  );

  if (res.status === 401) {
    // Token revoked or expired. Wipe and re-initiate device flow on the next
    // call so the user gets a fresh code instead of a silent bot fallback.
    await clearUserAuth();
    const retry = await resolveUserAuth();
    if (retry.kind === "pending") return pendingResult(retry.pending);
    // Fresh auth landed somehow - fall through to retry the post.
    const res2 = await fetch(
      `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/issues`,
      {
        method: "POST",
        headers: {
          Authorization: `token ${retry.auth.token}`,
          Accept: "application/vnd.github+json",
          "Content-Type": "application/json",
          "User-Agent": "ue-mcp",
        },
        body: JSON.stringify({ title, body, labels }),
      },
    );
    if (!res2.ok) {
      const text = await res2.text();
      throw new Error(`Failed to create issue as user (after re-auth): ${res2.status} ${text}`);
    }
    const issue2 = (await res2.json()) as { html_url: string; number: number };
    return {
      kind: "submitted",
      url: issue2.html_url,
      number: issue2.number,
      authoredBy: retry.auth.login,
      authoredAs: "user",
    };
  }

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Failed to create issue as user: ${res.status} ${text}`);
  }

  const issue = (await res.json()) as { html_url: string; number: number };
  return {
    kind: "submitted",
    url: issue.html_url,
    number: issue.number,
    authoredBy: auth.auth.login,
    authoredAs: "user",
  };
}
