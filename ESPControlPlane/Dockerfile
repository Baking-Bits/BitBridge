FROM node:20-alpine

WORKDIR /app/ESPControlPlane

COPY ESPControlPlane/package.json ./package.json
RUN npm install --omit=dev

COPY ESPControlPlane/src ./src
COPY index.html /app/index.html
COPY app.js /app/app.js
COPY styles.css /app/styles.css
COPY devices.config.js /app/devices.config.js
COPY packages /app/packages
COPY bin /app/bin

ENV NODE_ENV=production \
    CONTROL_PLANE_HOST=0.0.0.0 \
    CONTROL_PLANE_PORT=8787 \
    CONTROL_PLANE_CONFIG_DIR=/config \
    CONTROL_PLANE_STATIC_ROOT=/app

EXPOSE 8787

CMD ["node", "src/server.js"]
