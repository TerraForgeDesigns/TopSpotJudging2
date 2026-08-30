import pytest
from sqlalchemy import create_engine
from sqlalchemy.orm import Session

from app.models import Base


@pytest.fixture()
def db_session():
    """A fresh in-memory SQLite database per test — fast, isolated, no file I/O."""
    engine = create_engine("sqlite:///:memory:")
    Base.metadata.create_all(engine)
    with Session(engine) as session:
        yield session
    engine.dispose()
