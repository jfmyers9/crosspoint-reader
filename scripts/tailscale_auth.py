Import("env")

import os


auth_key = os.environ.get("CROSSPOINT_TAILSCALE_AUTH_KEY", "")
if auth_key:
    if '"' in auth_key or "\\" in auth_key:
        raise ValueError("CROSSPOINT_TAILSCALE_AUTH_KEY contains an unsupported character")
    env.Append(CPPDEFINES=[("CROSSPOINT_TAILSCALE_AUTH_KEY", f'\\"{auth_key}\\"')])
