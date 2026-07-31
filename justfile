#!/usr/bin/env -S just --justfile

root := justfile_directory()
ffmpeg_static_dir := root + "/ffmpeg-static"
dockerfile := root + "/Dockerfile"

clean-ffmpeg:
    #!/usr/bin/env bash
    set -e
    rm -rf {{ ffmpeg_static_dir }}
    mkdir -p {{ ffmpeg_static_dir }}

build-ffmpeg: clean-ffmpeg
    #!/usr/bin/env bash
    podman build -t builder -f {{ dockerfile }} .
    podman create --name builder-temp builder

    podman cp builder-temp:/install/usr/local/include {{ ffmpeg_static_dir }}/
    podman cp builder-temp:/install/usr/local/lib {{ ffmpeg_static_dir }}/

    podman rm builder-temp

clean:
    cmake --build cmake-build-release --target clean -j $(nproc)

build:
    cmake --build cmake-build-release --target libmsp -j $(nproc)
