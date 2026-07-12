FROM python:3.12-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        git \
        ca-certificates \
        curl \
        tar \
        xz-utils \
 && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir ufbt

# Cache the Flipper SDK in a writable location that can be backed by a volume.
ENV UFBT_HOME=/ufbt-cache
RUN mkdir -p "$UFBT_HOME"

COPY docker/entrypoint.sh /usr/local/bin/ufbt-wrapper
RUN chmod +x /usr/local/bin/ufbt-wrapper

WORKDIR /app

ENTRYPOINT ["/usr/local/bin/ufbt-wrapper"]
CMD ["build"]
