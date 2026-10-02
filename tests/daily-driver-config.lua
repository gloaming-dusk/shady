-- Deterministic compatibility profile used by the daily-driver sanitizer test.
-- Optional spatial modules are compiled out in this profile, so avoid naming
-- modules that may not be registered.
shady.set("spatial_mode", false)
shady.set("window_gravity", false)
shady.set("window_wobble", false)
shady.set("shadows", false)
shady.set("floor", false)
