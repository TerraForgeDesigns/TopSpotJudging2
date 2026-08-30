from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from app.api import router as api_router
from app.config import APP_DIR
from app.web import router as web_router

app = FastAPI(title="Top Spot Judging — Home Base")

app.mount("/static", StaticFiles(directory=str(APP_DIR / "static")), name="static")

app.include_router(web_router)
app.include_router(api_router)
