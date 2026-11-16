# Copies the shared dashboard (../../web/index.html) into data/ so that
# `pio run -t uploadfs` puts it on the hub's LittleFS for the local web UI.
import gzip, os, shutil
Import("env")  # noqa: F821  (provided by PlatformIO)
root = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(root, "..", "..", "web", "index.html")
dst_dir = os.path.join(root, "data")
os.makedirs(dst_dir, exist_ok=True)
# The hub serves the gzipped copy (about a quarter of the size): a 67 KB page
# sent to two phones at once could run the ESP32's network buffers dry.
if os.path.exists(src):
    plain = os.path.join(dst_dir, "index.html")
    if os.path.exists(plain):
        os.remove(plain)
    with open(src, "rb") as f, gzip.open(os.path.join(dst_dir, "index.html.gz"), "wb", 9) as g:
        shutil.copyfileobj(f, g)
