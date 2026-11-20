# PlatformIO pre-build script: minify and gzip the web app and the OpenAPI spec before
# they are embedded in the firmware (board_build.embed_files). The web server sends them
# with Content-Encoding: gzip (routesBasic.h).
#
# Minifying (on by default) saves flash on top of gzip:
#   - web/app.html: the <style> and <script> blocks go through esbuild (pinned version, run
#     with npx; needs Node.js), comments and the whitespace between tags are removed.
#     esbuild does not rename top-level names of a plain script, so names used from the
#     HTML (onclick=...) keep working.
#   - docs/openapi.json: written without indentation.
# Without Node.js / npx, or when esbuild fails, the web app is embedded as it is (with a
# warning), so a build never breaks on this.
#
# Options:
#   custom_minify_webapp = yes | no      in platformio.ini (per environment)
#   OPB_MINIFY=0 / 1                     environment variable, overrides the option
#
# Output next to the source (*.gz, not in git), with a fixed timestamp so the bytes are
# reproducible; only made again when the source or the minify setting changed.
Import("env")
import gzip, json, os, re, shutil, subprocess

ESBUILD = "esbuild@0.24.0"

root = env.subst("$PROJECT_DIR")
opt = os.environ.get("OPB_MINIFY")
if opt is None:
    opt = env.GetProjectOption("custom_minify_webapp", "yes")
minify = str(opt).strip().lower() in ("1", "yes", "true", "on")

def esbuild(code, loader):
    """Minified code, or None when esbuild is not available or fails"""
    npx = shutil.which("npx")
    if not npx:
        return None
    try:
        r = subprocess.run([npx, "--yes", ESBUILD, "--minify", "--loader=" + loader, "--log-level=error"],
                           input=code.encode("utf-8"), capture_output=True, timeout=180)
    except Exception as e:
        print(f"minify: esbuild could not run ({e})")
        return None
    if r.returncode:
        print("minify: esbuild failed: " + r.stderr.decode("utf-8", "replace")[:400])
        return None
    return r.stdout.decode("utf-8").strip()

def minify_html(html):
    """Minified app.html, or None (then the original is used)"""
    parts, pos = [], 0
    for m in re.finditer(r"(<(style|script)(\s[^>]*)?>)(.*?)(</\2>)", html, re.S | re.I):
        parts.append(("html", html[pos:m.start()]))
        body = m.group(4)
        if m.group(2).lower() == "script" and "src=" in (m.group(3) or "") or not body.strip():
            parts.append(("raw", m.group(0)))
        else:
            out = esbuild(body, "css" if m.group(2).lower() == "style" else "js")
            if out is None:
                return None
            parts.append(("raw", m.group(1) + out + m.group(5)))
        pos = m.end()
    parts.append(("html", html[pos:]))
    res = []
    for kind, text in parts:
        if kind == "html":
            text = re.sub(r"<!--(?!\[if).*?-->", "", text, flags=re.S)   # comments
            text = re.sub(r">\s+<", "> <", text)                         # whitespace between tags: one space
            text = re.sub(r"\n\s*", "\n", text)
        res.append(text)
    return "".join(res)

def write_gz(dst, data):
    with open(dst, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, compresslevel=9, mtime=0) as gz:
            gz.write(data)

stamp_file = os.path.join(env.subst("$BUILD_DIR"), "gzip_assets.stamp")
try:
    stamp = json.load(open(stamp_file))
except Exception:
    stamp = {}

for rel in ("web/app.html", "docs/openapi.json"):
    src = os.path.join(root, rel)
    dst = src + ".gz"
    key = f"{os.path.getmtime(src)}|minify={minify}"
    if os.path.exists(dst) and stamp.get(rel) == key:
        continue
    with open(src, "rb") as f:
        data = f.read()
    how = "gzip"
    if minify:
        if rel.endswith(".json"):
            data = json.dumps(json.loads(data), ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            how = "minify + gzip"
        else:
            out = minify_html(data.decode("utf-8"))
            if out is None:
                print(f"minify: {rel} is embedded without minifying (install Node.js for npx {ESBUILD}, or set custom_minify_webapp = no)")
            else:
                data = out.encode("utf-8")
                how = "minify + gzip"
    write_gz(dst, data)
    stamp[rel] = key
    print(f"{how} {rel}: {os.path.getsize(src)} -> {len(data)} -> {os.path.getsize(dst)} bytes")

os.makedirs(os.path.dirname(stamp_file), exist_ok=True)
json.dump(stamp, open(stamp_file, "w"))
