Import("env")
import os
import sys
import subprocess


def upload_ble_ota(source, target, env):
    firmware = str(source[0])
    script   = os.path.join(env["PROJECT_DIR"], "ble_ota.py")

    try:
        import bleak  # noqa: F401
    except ImportError:
        print("\nERROR: 'bleak' is not installed.")
        print("Run:  pip install bleak\n")
        env.Exit(1)
        return

    ret = subprocess.run([sys.executable, script, firmware])
    if ret.returncode != 0:
        env.Exit(ret.returncode)


env.Replace(UPLOADCMD=upload_ble_ota)
