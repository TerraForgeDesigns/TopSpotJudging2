"""
Show setup and the single "active show" every other page reads from.
See CONTEXT.md — Home Base is the source of truth, and one show is
active at a time.

Show CREATION lives in services/show_wizard.py (the six-step wizard is
now the only way to create a show — see CONTEXT.md's Show setup
section). This module keeps list/switch/edit, since those aren't part
of the wizard's job.
"""
from datetime import date

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import AppSettings, Show

_SETTINGS_ID = 1


def _get_or_create_settings(db: Session) -> AppSettings:
    settings = db.get(AppSettings, _SETTINGS_ID)
    if settings is None:
        settings = AppSettings(id=_SETTINGS_ID, active_show_id=None)
        db.add(settings)
        db.commit()
        db.refresh(settings)
    return settings


def get_active_show(db: Session) -> Show | None:
    settings = _get_or_create_settings(db)
    if settings.active_show_id is None:
        return None
    return db.get(Show, settings.active_show_id)


def set_active_show(db: Session, show_id: int) -> Show:
    show = db.get(Show, show_id)
    if show is None:
        raise ValueError(f"No show with id {show_id}")
    settings = _get_or_create_settings(db)
    settings.active_show_id = show_id
    db.commit()
    return show


def list_shows(db: Session) -> list[Show]:
    return list(db.scalars(select(Show).order_by(Show.event_date.desc())))


def update_show(
    db: Session, show_id: int, name: str, event_date: date, location: str | None, notes: str | None
) -> Show:
    show = db.get(Show, show_id)
    if show is None:
        raise ValueError(f"No show with id {show_id}")
    show.name = name.strip()
    show.event_date = event_date
    show.location = (location or "").strip() or None
    show.notes = (notes or "").strip() or None
    db.commit()
    db.refresh(show)
    return show
