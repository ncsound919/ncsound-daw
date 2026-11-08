# Production image for the collab relay (PRODUCTION.md).
# Build: docker build -t ncsound-collab -f collab/Dockerfile collab
FROM node:22-alpine AS build

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY public ./public
COPY src ./src
COPY tsconfig.json ./
# Build the phone/host browser bundles so public/*.js exists for the server.
RUN npm run build

FROM node:22-alpine AS run

WORKDIR /app
COPY package.json package-lock.json ./
# tsx is a devDependency but the runtime command needs it; install production
# deps plus tsx explicitly instead of shipping the whole toolchain.
RUN npm ci --omit=dev && npm install -g tsx@^4.21.0
COPY --from=build /app/public ./public
COPY --from=build /app/src ./src

# Non-root, read-only-ish, no build tools in the runtime image.
RUN addgroup -S app && adduser -S app -G app && chown -R app:app /app
USER app

ENV PORT=8787
ENV HOST=0.0.0.0
ENV NODE_ENV=production

EXPOSE 8787

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:8787/api/health').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["tsx", "src/server/index.ts"]
