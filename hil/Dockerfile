FROM python:3.12-slim

# System deps: openocd for STM32 flashing, esptool deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    openocd \
    udev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /hil

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt \
    && pip install --no-cache-dir esptool

COPY . .

# Device nodes are passed via --device flags in docker-compose
# USB serial and ST-Link are exposed to container at runtime

CMD ["pytest", "tests/", "-v", "--tb=short"]
