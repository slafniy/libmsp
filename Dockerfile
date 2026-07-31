#=======================================================================================================================
# STAGE 1: Build ffmpeg static libs
#=======================================================================================================================
FROM ubuntu:24.04 AS ffmpeg-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    ca-certificates \
    build-essential \
    pkg-config \
    zlib1g-dev \
    cmake \
    libpulse-dev \
    libasound2-dev \
    libjack-jackd2-dev \
    libsndio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /ffmpeg

#RUN git clone --depth 1 https://git.ffmpeg.org/ffmpeg.git -b release/9.0 .
RUN git clone --depth 1 https://github.com/FFmpeg/FFmpeg.git -b release/9.0 .


# I can enable ASM with this var, but ffmpeg would not link with my shared libmsp.so.
# AFAIU that's how ffmpeg asm is written and nothing I can do.
# But it shouldn't be an issue because I only use it to decode audio
#ENV ASFLAGS="-DPIC -f elf64"

RUN ./configure \
    --enable-pic \
    --extra-cflags="-fPIC" \
    --enable-static \
    --disable-iconv \
    --disable-debug \
    --disable-runtime-cpudetect \
    --disable-x86asm \
    --disable-shared \
    --disable-everything \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-autodetect \
    --enable-small \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-decoder=mp3,aac,flac,vorbis,opus,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_u8 \
    --enable-demuxer=mp3,mov,flac,ogg,wav,aac \
    --enable-parser=mpegaudio,aac,flac,opus \
    --enable-protocol=file

RUN make -j$(nproc)

RUN make install DESTDIR=/install

#=======================================================================================================================
# STAGE 1.1 Prepare ffmpeg static libs for output
#=======================================================================================================================
FROM scratch AS ffmpeg-export
COPY --from=ffmpeg-builder /install/usr/local/include/ /include
COPY --from=ffmpeg-builder /install/usr/local/lib/ /lib

#=======================================================================================================================
# STAGE 2 Build libmsp
#=======================================================================================================================
FROM ffmpeg-builder AS libmsp-builder

RUN mkdir "/libmsp"

WORKDIR /libmsp

# copy ffmpeg static libs where cmake whants them (to use the same scheme as local build)
RUN mkdir "ffmpeg-static" && cp -r /install/usr/local/* ./ffmpeg-static

COPY src /libmsp/src
COPY testapp /libmsp/testapp
COPY CMakeLists.txt /libmsp

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build  -j $(nproc)

#=======================================================================================================================
# STAGE 2.1 Prepare libmsp distribution for output
#=======================================================================================================================
FROM scratch AS libmsp-export
COPY --from=libmsp-builder /libmsp/build/libmsp.so* /
COPY --from=libmsp-builder /libmsp/build/testapp /
