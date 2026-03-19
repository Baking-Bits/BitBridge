FROM node:lts-slim

WORKDIR /app

# Install OpenSSH client for CLI SSH fallback patterns and diagnostics
RUN apt-get update && apt-get install -y --no-install-recommends openssh-client \
  && rm -rf /var/lib/apt/lists/*

# Install production dependencies first for better layer caching
COPY package*.json ./
RUN npm install --omit=dev --no-audit --no-fund

# Copy app source
COPY src ./src
COPY README.md ./README.md

ENV NODE_ENV=production
ENV HOMELAB_CONFIG_DIR=/config
ENV HOMELAB_ENV_FILE=/config/.env
ENV HOMELAB_CONFIG_FILE=/config/config.json
ENV BRIDGE_HOST=0.0.0.0
ENV BRIDGE_PORT=8787

EXPOSE 8787

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "const h=require('http');const p=process.env.BRIDGE_PORT||8787;const r=h.request({host:'127.0.0.1',port:p,path:'/health',timeout:2000},(res)=>process.exit(res.statusCode===200?0:1));r.on('error',()=>process.exit(1));r.end();"

CMD ["node", "src/bridge-server.js"]