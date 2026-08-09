# Bombe

Bombe is a deduction puzzle game built with C++20, SDL 2, and Z3. The repository also contains a grid generator and the score/level server.

## Repository contents

- `Bombe` — the game client
- `GridGenerator` — puzzle grid generation utility
- `BombeServer` — score and level server
- `Grid.cpp` / `Grid.h` — grid, region, rule, and solver logic
- `GameState.cpp` / `GameState.h` — game state, input, and rendering
- `levels.data`, `texture.png`, `snd/`, and `tutorial/` — runtime assets

## Building on Linux

Clone the repository with its clipboard helper submodule:

```sh
git clone --recurse-submodules <repository-url>
cd Bombe
```

If the repository was cloned without submodules, initialize them with:

```sh
git submodule update --init --recursive
```

### Dependencies

Debian or Ubuntu:

```sh
sudo apt install \
  autoconf automake g++ make pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-net-dev libsdl2-ttf-dev \
  libz3-dev libzstd-dev libcurl4-openssl-dev \
  libxcb1-dev libx11-dev libpng-dev
```

Fedora:

```sh
sudo dnf install \
  autoconf automake gcc-c++ make pkgconf-pkg-config \
  SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_net-devel SDL2_ttf-devel \
  z3-devel libzstd-devel libcurl-devel \
  libxcb-devel libX11-devel libpng-devel
```

### Configure and compile

Generate `configure`, `Makefile.in`, and the other required Autotools helper files using `--install`:

```sh
autoreconf --install --force
./configure --disable-steam
make -j"$(nproc)"
```

This builds all three executables in the repository root. To build only the game:

```sh
make -j"$(nproc)" Bombe
```

The default Steam-enabled configuration requires the Steamworks SDK headers and `libsteam_api`. Use `--disable-steam` unless those are available locally.

### Faster development builds

When generated-code performance and debugging symbols are unimportant, disable optimization and debug information:

```sh
make -j"$(nproc)" CXXFLAGS="-O0 -g0 -pipe"
```

After changing `configure.ac` or `Makefile.am`, regenerate the build system before configuring again:

```sh
autoreconf --install --force
./configure --disable-steam
```

## Running

Run the game from the repository root so it can find its fonts, textures, sounds, translations, tutorials, and level data:

```sh
./Bombe
```

## Server configuration

`BombeServer` uses libcurl for Steam ticket authentication. Set the Steam Web API key in the environment before running a production server:

```sh
export BOMBE_STEAM_WEB_API_KEY="your-api-key"
./BombeServer
```

Do not commit API keys to the repository.

## License

This project is licensed under the terms in [LICENSE](LICENSE).
