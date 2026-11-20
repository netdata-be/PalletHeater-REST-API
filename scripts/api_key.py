# PlatformIO pre script: the API key of the controller for the network uploads (espota
# password, X-API-Key header of POST /api/update). Taken from the environment, or else from
# .envrc in the project folder (git-ignored; direnv format):
#   OPB_API_KEY=<key>            or   export OPB_API_KEY=<key>
# The key is shown in the web app (Settings) and by GET /api/auth.
# Building never needs the key; only an upload stops, with a clear message, when it is not set.
import os
import re
Import("env")  # noqa: F821


def _from_envrc(path):
    """OPB_API_KEY from a .envrc / .env file: KEY=value lines, optional export and quotes"""
    try:
        with open(path) as f:
            for line in f:
                m = re.match(r"\s*(?:export\s+)?OPB_API_KEY\s*=\s*(.*?)\s*$", line)
                if m:
                    v = m.group(1)
                    if len(v) >= 2 and v[0] == v[-1] and v[0] in "'\"":
                        v = v[1:-1]
                    else:
                        v = v.split(" #")[0].strip()   # trailing comment
                    return v
    except OSError:
        pass
    return ""


key = os.environ.get("OPB_API_KEY", "").strip()
source = "environment"
if not key:
    key = _from_envrc(os.path.join(env.subst("$PROJECT_DIR"), ".envrc"))
    source = ".envrc"

env["OPB_API_KEY"] = key  # used as $OPB_API_KEY in upload_command (http env)
if key:
    os.environ["OPB_API_KEY"] = key   # for ble_ota.py, started by the ble-ota upload
if key and env.GetProjectOption("upload_protocol", "") == "espota":
    env.Append(UPLOAD_FLAGS=["--auth=" + key])


def _need_key(*args, **kwargs):
    if not key:
        print("\nOPB_API_KEY is not set: put OPB_API_KEY=<key> in .envrc in the project folder, or\n"
              "export OPB_API_KEY=<key> (key: web app Settings, or GET /api/auth), then upload again.\n")
        env.Exit(1)
    print("API key for the upload taken from the %s" % source)


env.AddPreAction("upload", _need_key)
