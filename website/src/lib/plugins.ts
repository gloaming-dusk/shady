export const builtinPlugins = [
  { name: 'window-motion', description: 'Wobble and tilt for spatial windows, driven through the public motion API.', source: 'examples/plugins/window_motion.c' },
  { name: 'obj-loader', description: 'Loads OBJ environments, including authored collision geometry.', source: 'loaders/obj/obj_loader.c' }
];

// External means a separately loaded native plugin, including repository examples.
// Build defaults come from meson.build; these plugins are not auto-loaded.
export const externalPlugins = [
  { name: 'afterglow', description: 'A camera-aware sky and sea, four times of day, and sinking close animations.', note: 'Requires spatial + close-animation. Super+T cycles the hour.' },
  { name: 'water-windows', description: 'Animated liquid surfaces on live windows.', note: 'Requires spatial. Super+W toggles the effect.' },
  { name: 'focus-depth', description: 'Moves focused and background windows through depth while preserving screen-space placement.', note: 'Requires spatial.' },
  { name: 'spatial-overview', description: 'A native 3D window overview that restores the original layout when closed.', note: 'Requires spatial. Avoid combining with other position writers.' },
  { name: 'magnetic-windows', description: 'Spring-docks nearby windows into spatial structures, with ABI v2 state migration.', note: 'Requires spatial. Super+M toggles docking.' },
  { name: 'window-constellation', description: 'Orbits windows around the focused window and spring-restores their original layout.', note: 'Requires spatial. Super+C toggles the constellation.' },
  { name: 'window-portal', description: 'A refractive circular portal into another live Wayland window.', note: 'Requires spatial. Super+P toggles the portal.' },
  { name: 'black-hole', description: 'Swallows workspace windows into a black hole and releases them through a white hole.', note: 'Requires spatial. Super+H toggles the effect.' },
  { name: 'frozen-window', description: 'Frost crystallises across a window and melts away again, without touching its layout or input.', note: 'Requires spatial. Super+Z freezes the focused window; Super+Shift+Z the workspace.' },
  { name: 'astral-loom', description: 'Procedural sky, glass window representations, and animated spatial layouts.', note: 'Requires spatial. Includes shaders; the associated rice is archived.' },
  { name: 'fps-cube', description: 'Cube-shaped window bodies for FPS interaction.', note: 'Requires fps. Choose one FPS representation plugin at a time.' },
  { name: 'fps-squash', description: 'Animated squash representations for FPS window bodies.', note: 'Requires fps. Choose one FPS representation plugin at a time.' },
  { name: 'fps-jelly', description: 'Springy jelly window bodies for FPS interaction.', note: 'Requires fps. Choose one FPS representation plugin at a time.' },
  { name: 'fps-folded-paper', description: 'Folded-paper mesh window bodies with convex collision geometry.', note: 'Requires fps. Choose one FPS representation plugin at a time.' },
  { name: 'fps-origami', description: 'Spring-driven origami meshes with compound collision bodies.', note: 'Requires fps. Choose one FPS representation plugin at a time.' },
  { name: 'border-accent', description: 'Toggles a per-window border accent on the focused window.', note: 'Super+B toggles the accent.' },
  { name: 'close-slide-fade', description: 'Replaces the default crumple close animation with a slide and fade.', note: 'Requires spatial + close-animation. Choose one close-effect plugin.' },
  { name: 'close-burn', description: 'Adds a shader-driven burn effect to closing windows.', note: 'Requires spatial + close-animation. Choose one close-effect plugin.' },
  { name: 'shader-overlay', description: 'A fullscreen animated shader overlay using host-managed render hooks.', note: 'Includes external shader files.' },
  { name: 'orbit-layout', description: 'Places the focused window in a primary slot with surrounding satellite windows.', note: 'Requires spatial. Build explicitly.', manualBuild: true },
  { name: 'hello', description: 'A minimal native plugin example for learning the entry and lifecycle APIs.', note: 'Build explicitly. Module name: hello-plugin.', manualBuild: true },
  { name: 'counter', description: 'A stateful example for testing ABI v2 reload and state migration.', note: 'Build explicitly. Module name: counter-plugin.', manualBuild: true }
];

export const coreModules = [
  ['desktop-protocols', 'Desktop Wayland protocol integration.'],
  ['workspace', 'Named workspaces, window membership, and visibility.'],
  ['spatial', 'Shared 3D world, camera, and window depth.'],
  ['physics', 'Gravity, velocity, and spatial collisions.'],
  ['fps', 'First-person capture, grabbing, and expanded window bodies.'],
  ['close-animation', 'Per-window close animation lifecycle.'],
  ['scene-effects', 'Scene rendering effects.'],
  ['lua', 'Runtime scripting, events, and key bindings.']
];
