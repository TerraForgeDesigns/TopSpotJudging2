import pytest
from fastapi.testclient import TestClient
from sqlalchemy import create_engine
from sqlalchemy.orm import Session
from sqlalchemy.pool import StaticPool

from app.db import get_db
from app.models import Base


@pytest.fixture()
def db_session():
    """A fresh in-memory SQLite database per test — fast, isolated, no file
    I/O. StaticPool + check_same_thread=False: TestClient (see the `client`
    fixture below) runs requests on a separate thread, and a bare
    ":memory:" engine hands out a brand-new empty database per connection —
    StaticPool keeps everyone on the one connection that actually has the
    schema on it."""
    engine = create_engine(
        "sqlite:///:memory:", connect_args={"check_same_thread": False}, poolclass=StaticPool
    )
    Base.metadata.create_all(engine)
    with Session(engine) as session:
        yield session
    engine.dispose()


@pytest.fixture()
def client(db_session):
    """A TestClient wired to the same in-memory session as db_session, so a
    test can set up fixtures via the services layer and then exercise the
    real HTTP routes against that exact state."""
    from app.main import app

    def _override_get_db():
        yield db_session

    app.dependency_overrides[get_db] = _override_get_db
    with TestClient(app) as test_client:
        yield test_client
    app.dependency_overrides.clear()
