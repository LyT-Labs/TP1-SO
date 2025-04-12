#!/bin/bash

# Nombre del contenedor
CONTAINER_NAME="TP1_SO"

# Nombre de la imagen oficial del TP
IMAGE="agodio/itba-so-multi-platform:3.0"

# Ruta del directorio actual
TP_DIR=$(pwd)

# Nombre de la carpeta montada dentro del contenedor
CONTAINER_DIR="/tp"

echo "==> Corriendo contenedor con entorno del TP..."
echo "==> Montando carpeta actual en $CONTAINER_DIR"
echo "==> Nombre del contenedor: $CONTAINER_NAME"

docker run \
  --memory=512m \
  --memory-swap=512m \
  --cpus=0.5 \
  --pids-limit=100 \
  --tmpfs /tmp \
  --name "$CONTAINER_NAME" \
  -d \
  -v "$TP_DIR":"$CONTAINER_DIR" \
  -w "$CONTAINER_DIR" \
  "$IMAGE" \
  /bin/bash -c "while true; do sleep 1; done"

