// Separate service: this can run on a PC instead of the Raspberry Pi.
const fs = require('fs');
const path = require('path');
async function start() {
  const token = path.join(require('os').tmpdir(), 'anonymous_token');
  if (!fs.existsSync(token)) fs.writeFileSync(token, '', { mode: 0o600 });
  const { serveNcmApi } = require('@neteaseapireborn/api/server');
  await serveNcmApi({ host: process.env.HOST || '127.0.0.1', port: Number(process.env.PORT || 3000) });
  // Do not block local service readiness on an external token refresh.
  require('@neteaseapireborn/api/generateConfig')().catch(error => console.error(error.message));
}
start().catch(error => { console.error(error.message); process.exitCode = 1; });
