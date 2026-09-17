from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from app.api import router as api_router
from app.config import APP_DIR, PHOTOS_DIR
from app.services.discovery import DiscoveryResponderThread
from app.services.sd_card_watcher import SdCardWatcherThread
from app.web import router as web_router


@asynccontextmanager
async def lifespan(app: FastAPI):
    watcher = SdCardWatcherThread()
    discovery = DiscoveryResponderThread()
    watcher.start()
    discovery.start()
    yield
    discovery.stop()
    watcher.stop()


app = FastAPI(title="Top Spot Judging — Home Base", lifespan=lifespan)

app.mount("/static", StaticFiles(directory=str(APP_DIR / "static")), name="static")
app.mount("/photo-files", StaticFiles(directory=str(PHOTOS_DIR)), name="photo_files")

app.include_router(web_router)
app.include_router(api_router)
