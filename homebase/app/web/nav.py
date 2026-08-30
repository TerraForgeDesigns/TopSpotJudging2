"""
Primary navigation for the app shell. `enabled=False` items render as
disabled nav entries rather than dead links — this repo is scaffold-only
right now (see CONTEXT.md) and an honest "not built yet" beats a 404.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class NavItem:
    label: str
    path: str
    enabled: bool = True


NAV_ITEMS: list[NavItem] = [
    NavItem("Dashboard", "/"),
    NavItem("Shows", "/shows"),
    NavItem("Cars", "/cars"),
    NavItem("Judging Criteria", "/criteria"),
    NavItem("Classes", "/classes"),
    NavItem("Photos", "/photos", enabled=False),
    NavItem("Results", "/results", enabled=False),
    NavItem("Awards", "/awards", enabled=False),
    NavItem("Styleguide", "/styleguide"),
]
