from datetime import datetime, timezone


def relative_time(dt: datetime | None, *, now: datetime | None = None) -> str:
    """'2 min ago' style relative timestamp. Computed server-side, freshly,
    on every request — the dashboard polls every 15s (see PROTOCOL.md-style
    sync cadence), so there's no need for a ticking client-side clock."""
    if dt is None:
        return "Never"

    now = now or datetime.now(timezone.utc)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)

    delta_seconds = (now - dt).total_seconds()
    if delta_seconds < 0:
        delta_seconds = 0

    if delta_seconds < 45:
        return "Just now"
    if delta_seconds < 90:
        return "1 min ago"
    if delta_seconds < 3600:
        return f"{int(delta_seconds // 60)} min ago"
    if delta_seconds < 7200:
        return "1 hr ago"
    if delta_seconds < 86400:
        return f"{int(delta_seconds // 3600)} hr ago"
    days = int(delta_seconds // 86400)
    return f"{days} day{'s' if days != 1 else ''} ago"
