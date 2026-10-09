# Upstream upgrades and local builds

## Current baseline

The `dev` branch contains DDNet 20.1.1 from upstream commit `647a2db7e3581c1deb45ff5dd877a41278a22798`. It is imported under `TeeTycoon/` as a Git subtree, so later upstream releases can be merged without copying the full source by hand. `main` remains the original DDNet 15.9.1-based baseline.

The upstream remote is named `upstream`. Update it on `dev` with:

```powershell
git fetch upstream --tags
git subtree pull --prefix=TeeTycoon upstream <DDNet-tag>
git submodule update --init --recursive
```

Resolve conflicts in TeeTycoon-modified upstream files, rebuild, and commit the resolved port on `dev`. The root `.gitmodules` maps upstream's `ddnet-libs` dependency to `TeeTycoon/ddnet-libs`; keep that path mapping when updating the subtree.

## Server build on Windows

Use a Visual Studio Developer PowerShell with the Native Desktop C++ workload, CMake, Ninja, and Rust 1.85.0 installed. Configure and build in a versioned directory under the ignored `TeeTycoon/out/build/` folder so old build outputs and runtime databases stay untouched.

```powershell
$env:RUSTUP_TOOLCHAIN = "1.85.0"
cmake -S TeeTycoon -B TeeTycoon/out/build/<version>-msvc -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCLIENT=OFF -DSERVER=ON -DPREFER_BUNDLED_LIBS=ON
cmake --build TeeTycoon/out/build/<version>-msvc --target game-server
```

For the verified DDNet 20.1.1 build, the directory is `TeeTycoon/out/build/20.1.1-msvc`. Its `Accounts.sqlite`, `ddnet-server.sqlite`, event state files, and server configuration were copied from the prior active build. Keep runtime data in `out`; it is ignored by Git.

The port compiles the server on MSVC. Some mod behavior still lives in DDNet core files, so upgrades can still need manual conflict resolution; future work should continue moving TeeTycoon-only behavior into its own server module.
