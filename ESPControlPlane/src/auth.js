import jwt from "jsonwebtoken";

const COOKIE_NAME = "bcp_session";
const TOKEN_EXPIRY = "8h";

/**
 * Signs a new session JWT using the control plane token as the secret.
 */
export function signSession(secret) {
  return jwt.sign({ auth: true }, secret, { expiresIn: TOKEN_EXPIRY });
}

/**
 * Verifies a session JWT. Returns the decoded payload or null if invalid/expired.
 */
export function verifySession(token, secret) {
  try {
    return jwt.verify(token, secret);
  } catch {
    return null;
  }
}

export { COOKIE_NAME };
