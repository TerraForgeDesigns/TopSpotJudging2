"""
Application configuration. Plain env-var reads — no external config service,
no internet lookups. See CONTEXT.md offline-first constraint.
"""
import os
from pathlib import Path

APP_DIR = Path(__file__).resolve().parent
HOMEBASE_DIR = APP_DIR.parent
DATA_DIR = HOMEBASE_DIR / "data"
DATA_DIR.mkdir(exist_ok=True)

DATABASE_URL = os.environ.get(
    "TOPSPOT_DATABASE_URL",
    f"sqlite:///{(DATA_DIR / 'topspot.db').as_posix()}",
)

PHOTOS_DIR = DATA_DIR / "photos"
PHOTOS_DIR.mkdir(exist_ok=True)

HOST = os.environ.get("TOPSPOT_HOST", "0.0.0.0")
PORT = int(os.environ.get("TOPSPOT_PORT", "8000"))
SERVICE_NAME = "topspot-homebase"
PROTOCOL_VERSION = 1
DISCOVERY_HOSTNAME = os.environ.get("TOPSPOT_DISCOVERY_HOSTNAME", "topspot-homebase.local")
DISCOVERY_PORT = int(os.environ.get("TOPSPOT_DISCOVERY_PORT", "37020"))
