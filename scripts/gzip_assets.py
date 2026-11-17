# PlatformIO pre-build script: gzip the web app and the OpenAPI spec before they are
# embedded in the firmware (board_build.embed_files). Saves ~240 KB of flash; the
# web server sends them with Content-Encoding: gzip (routesBasic.h).
# Output next to the source (*.gz, not in git); only rewritten when the source
# changed, and with a fixed timestamp so the bytes are reproducible.
Import("env")
import gzip, os

root = env.subst("$PROJECT_DIR")
for rel in ("web/app.html", "docs/openapi.json"):
    src = os.path.join(root, rel)
    dst = src + ".gz"
    if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
        continue
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, compresslevel=9, mtime=0) as gz:
            gz.write(data)
    print(f"gzip {rel}: {len(data)} -> {os.path.getsize(dst)} bytes")
