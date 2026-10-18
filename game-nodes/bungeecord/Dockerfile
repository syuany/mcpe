# ==============================================================================
# BungeeCord 网关代理服务 Dockerfile (自动拉取官方最新 Jar)
# ==============================================================================
FROM eclipse-temurin:17-jre-alpine

RUN apk add --no-cache curl bash ca-certificates && \
    addgroup -S bungeegroup && adduser -S bungee -G bungeegroup

WORKDIR /server

# 自动从官方 Jenkins CI 拉取最新稳定版 BungeeCord.jar
ARG BUNGEECORD_URL=https://ci.md-5.net/job/BungeeCord/lastSuccessfulBuild/artifact/bootstrap/target/BungeeCord.jar
RUN curl -fSL "${BUNGEECORD_URL}" -o /server/BungeeCord.jar || \
    curl -fSL "https://download.geysermc.org/v2/projects/bungeecord/versions/latest/builds/latest/downloads/bungeecord" -o /server/BungeeCord.jar || true

# 拷贝默认路由配置
COPY config.yml /server/config.yml

RUN chown -R bungee:bungeegroup /server
USER bungee

EXPOSE 25565

ENTRYPOINT ["java", "-Xms256M", "-Xmx512M", "-jar", "/server/BungeeCord.jar"]
