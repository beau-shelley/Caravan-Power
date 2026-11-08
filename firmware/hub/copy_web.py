# Copies the shared dashboard (../../web/index.html) into data/ so that
# `pio run -t uploadfs` puts it on the hub's LittleFS for the local web UI.
import os, shutil
Import("env")  # noqa: F821  (provided by PlatformIO)
root = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(root, "..", "..", "web", "index.html")
dst_dir = os.path.join(root, "data")
os.makedirs(dst_dir, exist_ok=True)
if os.path.exists(src):
    shutil.copyfile(src, os.path.join(dst_dir, "index.html"))
