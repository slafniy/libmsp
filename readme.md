[![last master build](https://github.com/slafniy/libmsp/actions/workflows/build_release.yml/badge.svg?branch=master)](https://github.com/slafniy/libmsp/actions/workflows/build_release.yml)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/slafniy/libmsp?color=blue&label=version)](https://github.com/slafniy/libmsp/releases)
### What is libmsp?
A small linux shared library which plays music files. Written in C.

### Features
- Depends only on libc (2.39+). If you have something equal or more modern than Ubuntu 24 it should just work.
- Supports the most popular music formats (mp3, flac, m4a, wav, aac, ogg) with different codecs in them.
- Built on top of ffmpeg and SDL3. I'm not that crazy (yet) to write so much code by myself.

### How it works / how to use
Public library interface is defined and documented in [libmsp.h](src/libmsp.h) header. 

libmsp uses two threads:
1. _Main thread_ to push user commands to the _playback thread_ with one exception: `msp_get_metadata()` 
works in the _main thread_. All other functions are expected to return instantly and won't block your app.
2. _Playback thread_ listens commands in a loop, and also does dirty decoding work if it requested to play something.  
It does not decode many frames ahead and does not waste much CPU.

Usage general steps are:
1. Initialize library context
2. Use functions to play, pause, etc
3. Deinitialize.

For usage example see [testapp.c](testapp/testapp.c) (WIP)

The minimal code sample:
```c++
#include "../src/libmsp.h"
#include <unistd.h>

int main() {
    msp_init();
    msp_play("music.mp3");
    sleep(10);  // without sleep msp_play() instantly returns and you won't hear anything
    msp_deinit();
    return 0;
}
```

### How to build
#### Prerequisites
- linux (for local build, container build _should_ work on any platform). I didn't test it anywhere else.
- cmake
- gcc
- just
- podman

#### Build
First of all, you can check ```just --list``` and see full list of available recipes.  

The build itself consists of two parts: 1. ffmpeg static build 2. every other stuff build. The 1st part historically  
builds only in a container, because I wanted to get reproducible builds while figuring out what demuxers and codecs I can 
and what I can't turn off. So for now even for a local build you need podman.  
The 2nd part - library itself - could be built in a container or locally.

#### Full build in podman
Ready for distribution release build  
```just podman-build```  
And see `./dist` folder for .so

#### Local debug build
Builds ffmpeg-static in podman, copies it to the host, and builds libmsp locally. Also builds _testapp_ binary.  
```just build```

### Why it exists?
1. Because I can :) 
2. I have a pet-project music player with Godot UI (because why not), and it needs a good playback backend.  
I tried LibVLC, and it is great, but it has a bunch of dependencies, basically it requires whole vlc package to work, 
and C# wrapper nuget package for LibVLC does not include any native libs at all. And I want a self-contained binary.  
Also, Godot does not include native libs into distribution by default, so you have to deal with LD_LIBRARY_PATH hacks etc.  
So, I decided to build own minimalistic library and ship it in nuget package. [C# wrapper is here.](https://github.com/slafniy/libmsp-sharp)  
3. This is a great opportunity for me to code something in a system language.

#### What MSP stands for?
It stands for "My Stupid Player"

