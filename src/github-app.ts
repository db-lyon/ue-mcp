import { createSign } from "node:crypto";

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

export async function submitFeedback(
  title: string,
  body: string,
  labels: string[] = ["agent-feedback"],
): Promise<{ url: string; number: number }> {
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
      },
      body: JSON.stringify({ title, body, labels }),
    },
  );

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Failed to create issue: ${res.status} ${text}`);
  }

  const issue = (await res.json()) as { html_url: string; number: number };
  return { url: issue.html_url, number: issue.number };
}
