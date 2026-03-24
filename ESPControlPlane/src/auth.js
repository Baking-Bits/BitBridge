import jwt from "jsonwebtoken";

const COOKIE_NAME = "bcp_session";
const TOKEN_EXPIRY = "8h";

/**
 * Signs a new session JWT for an authenticated DB user.
 */
export function signSession(secret, user) {
  return jwt.sign(
    {
      auth: true,
      sub: String(user.id),
      username: user.username,
      role: user.role || "admin"
    },
    secret,
    { expiresIn: TOKEN_EXPIRY }
  );
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
