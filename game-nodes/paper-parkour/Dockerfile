# ==============================================================================
# Paper 1.12.2 跑酷子服 Dockerfile
# ==============================================================================
FROM eclipse-temurin:17-jre-alpine

RUN apk add --no-cache curl bash jq ca-certificates && \
    addgroup -S papergroup && adduser -S paper -G papergroup

WORKDIR /server

# 接受 Mojang EULA
RUN echo "eula=true" > /server/eula.txt

# 自动从 PaperMC API 获取 1.12.2 最新稳定 build 并下载
ARG PAPER_VERSION=1.12.2
RUN PAPER_BUILD=$(curl -s https://api.papermc.io/v2/projects/paper/versions/${PAPER_VERSION} | jq -r '.builds[-1]') && \
    if [ "$PAPER_BUILD" != "null" ] && [ -n "$PAPER_BUILD" ]; then \
        curl -fSL "https://api.papermc.io/v2/projects/paper/versions/${PAPER_VERSION}/builds/${PAPER_BUILD}/downloads/paper-${PAPER_VERSION}-${PAPER_BUILD}.jar" -o /server/paper.jar; \
    else \
        curl -fSL "https://api.papermc.io/v2/projects/paper/versions/1.12.2/builds/1620/downloads/paper-1.12.2-1620.jar" -o /server/paper.jar || true; \
    fi

# 拷贝配置文件
COPY config/server.properties /server/server.properties
COPY config/spigot.yml /server/spigot.yml
COPY config/paper.yml /server/paper.yml

# 创建 plugins 目录
RUN mkdir -p /server/plugins && \
    chown -R paper:papergroup /server

USER paper

EXPOSE 25567

ENTRYPOINT ["java", "-Xms512M", "-Xmx1024M", "-XX:+UseG1GC", "-jar", "/server/paper.jar", "nogui"]
