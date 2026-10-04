"""Terminal content for real Wayland clients in the headless capture scene."""
import os
import sys
import time

name, pane = sys.argv[1], int(sys.argv[2])
print("\033[2J\033[H", end="")
print("\033[38;2;255;176;112mSHADY  /  " + name.upper() + "\033[0m\n")
if name in {"hello", "counter"}:
    # These examples have no rendering effect. Display their real host logs.
    print("  Native plugin / lifecycle example\n")
    with open(os.environ["SHADY_CAPTURE_LOG"]) as log:
        for line in log:
            if "[plugin]" not in line:
                continue
            message = line.split("[plugin] ", 1)[-1].strip()
            if any(token in message for token in ("hello plugin", "seat=", "output[", "counter ")):
                print("  " + message[:48])
    print("\n  No visual effect is registered.")
elif pane == 1:
    for row in range(5):
        print("".join(f"\033[48;2;{37+row*19};{74+column*10};{105+row*13}m  " for column in range(16)) + "\033[0m")
    print("\n  Live Wayland surface  /  GLES2")
elif pane == 2:
    print('  -- config.lua\n')
    print(f'  shady.plugins.load("{name}")\n')
    print('  Native C plugin\n  Host-managed rendering')
else:
    print("  Compositor: headless\n  Renderer:   software GLES2\n")
    print("  Three live Wayland clients\n  Captured with the Automation API")
sys.stdout.flush()
time.sleep(20)
