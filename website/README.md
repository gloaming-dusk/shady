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
- `src/pages/docs/` — routes backed directly by the repository `docs/` directory
