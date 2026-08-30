"""
Primary navigation for the app shell. Labels follow LANGUAGE.md (e.g.
"Judging Categories," not "Judging Criteria" — see that document's
mapping table). Every item below has a real route: the five reshaped by
the Aug 2026 spec update (Cars, Judging Categories, Conflicts, Results,
Awards) point at plain placeholder pages for now — see app/web/stubs.py
and DECISIONS.md — rather than a disabled link or a 404. Classes is
gone entirely: Car Class was removed by the spec update.
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
    NavItem("Judging Categories", "/criteria"),
    NavItem("Photos", "/photos"),
    NavItem("Conflicts", "/conflicts"),
    NavItem("Results", "/results"),
    NavItem("Awards", "/awards"),
    NavItem("Styleguide", "/styleguide"),
]
