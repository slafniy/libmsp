#!/usr/bin/env -S just --justfile

root := justfile_directory()

ffmpeg_static_dir := root + "/ffmpeg-static"
dockerfile := root + "/Dockerfile"
dist_dir := root + "/dist"

# Build libmsp in a container and copy to host
podman-build:
    #!/usr/bin/env bash
    set -e
    rm -rf {{ dist_dir }}
    podman build --target libmsp-export -t builder -f {{ dockerfile }} --output type=local,dest={{ dist_dir }} .
    echo "Build is finished, artifacts: {{ dist_dir }}"

# Build ffmpeg static libs in a container and copy to host
podman-build-ffmpeg:
    #!/usr/bin/env bash
    set -e
    rm -rf {{ ffmpeg_static_dir }}
    podman build --target ffmpeg-export -t builder -f {{ dockerfile }} --output type=local,dest={{ ffmpeg_static_dir }} .
    echo "ffmpeg build is finished, artifacts: {{ ffmpeg_static_dir }}"

# Build libmsp DEBUG locally (rebuilds ffmpeg static libs in podman anyway)
build: podman-build-ffmpeg
    cmake --build cmake-build-debug --target libmsp -j $(nproc)

# Build libmsp RELEASE locally. Not recommended for distribution.
build-release:
    cmake --build cmake-build-release --target libmsp -j $(nproc)

# Clean cmake
clean:
    cmake --build cmake-build-release --target clean -j $(nproc)
    cmake --build cmake-build-debug --target clean -j $(nproc)

