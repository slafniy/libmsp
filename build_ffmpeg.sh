#!/usr/bin/env bash
set -e

FFMPEG_STATIC_DIR="./ffmpeg-static"

rm -rf $FFMPEG_STATIC_DIR
mkdir -p $FFMPEG_STATIC_DIR

docker build -t ffmpeg-builder -f Dockerfile_build_ffmpeg .
docker create --name ffmpeg-builder-temp ffmpeg-builder

docker cp ffmpeg-builder-temp:/install/usr/local/include $FFMPEG_STATIC_DIR/
docker cp ffmpeg-builder-temp:/install/usr/local/lib $FFMPEG_STATIC_DIR/

docker rm ffmpeg-builder-temp