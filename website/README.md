# Shady website

The project website is a static [Astro](https://astro.build/) site.

Repository documentation is not duplicated into the site. The Astro content
collection loads Markdown directly from `../docs/*.md`, and the docs routes
render those files with site navigation and a generated table of contents.

## Development

From the repository root:

```sh
nix develop
cd website
npm ci
npm run dev
```

Quality checks:

```sh
npm run check
npm run build
```

The static output is written to `website/dist/`.

## Content

- `src/pages/index.astro` — landing page
- `src/pages/getting-started.astro` — installation and first-run guide
- `src/pages/demo/afterglow.astro` — featured Afterglow demo
- `src/pages/plugins.astro` — available plugins, core modules, and loading instructions
- `src/lib/plugins.ts` — plugin catalog; keep names and build status aligned with `meson.build` and `src/module/builtin.c`
- `src/pages/docs/` — routes backed directly by the repository `docs/` directory

## Plugin screenshots

Every plugin card uses a real 1280×720 capture from Shady's headless backend,
created with `shady.automation.screenshot()` and `grim`. The scenes use live
`foot` Wayland clients, activate the relevant effects, and capture animations
in progress. `hello` and `counter` have no visual effect, so their previews
show actual plugin logs; the counter scene also performs a stateful reload.
Core modules are documented separately and do not have plugin screenshots.

Regenerate all previews from the repository root:

```sh
nix develop
./build.sh
ninja -C build libshady-plugin-hello.so libshady-plugin-counter.so libshady-plugin-orbit-layout.so
python3 website/scripts/capture-plugin-previews.py
```

Pass plugin names to regenerate only selected previews:

```sh
python3 website/scripts/capture-plugin-previews.py water-windows fps-cube
```

The capture script creates an isolated runtime directory for each compositor,
uses software GLES2, and writes PNG originals to `src/assets/plugins/`. The
scene configuration and automation are in `scripts/plugin-previews/`.
The OBJ scene loads `assets/test-room.obj`. Full-size PNGs remain available
by clicking each preview; Astro generates lazy-loaded WebP thumbnails for
the cards. Regenerate captures after changes to plugin rendering or shaders.
