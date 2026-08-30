"""
Primary (sidebar) navigation for the app shell. Deliberately short now:
the Show Dashboard's eight sections (Overview, Cars, Judging, Handhelds,
Awards, Photos, Results, Edit Show — see CONTEXT.md) are their own tab
strip (components/macros.html's dashboard_tabs), not sidebar items — see
DECISIONS.md. The sidebar only holds things that exist independently of
whichever show is active: switching shows, and the styleguide.
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
    NavItem("Styleguide", "/styleguide"),
]
