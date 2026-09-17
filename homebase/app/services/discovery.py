import json
import logging
import socket
import threading

from app.config import DISCOVERY_HOSTNAME, DISCOVERY_PORT, PORT, PROTOCOL_VERSION, SERVICE_NAME

logger = logging.getLogger(__name__)


class DiscoveryResponderThread(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self._stop_event = threading.Event()
        self._sock: socket.socket | None = None

    def run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock = sock
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1.0)
        try:
            sock.bind(("", DISCOVERY_PORT))
        except OSError as exc:
            logger.warning("Top Spot UDP discovery disabled; bind failed on port %s: %s", DISCOVERY_PORT, exc)
            sock.close()
            return
        logger.info("Top Spot UDP discovery listening on 0.0.0.0:%s", DISCOVERY_PORT)

        while not self._stop_event.is_set():
            try:
                data, addr = sock.recvfrom(1024)
            except TimeoutError:
                continue
            except OSError:
                break

            try:
                payload = json.loads(data.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue

            if payload.get("service") != "topspot-handheld" or payload.get("query") != "discover":
                continue

            host = _best_response_host(addr[0])
            response = {
                "service": SERVICE_NAME,
                "status": "ok",
                "protocol_version": PROTOCOL_VERSION,
                "hostname": DISCOVERY_HOSTNAME,
                "base_url": f"http://{host}:{PORT}",
            }
            sock.sendto(json.dumps(response).encode("utf-8"), addr)
            logger.info("Answered handheld discovery request from %s with %s", addr[0], response["base_url"])

        sock.close()

    def stop(self) -> None:
        self._stop_event.set()
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass


def _best_response_host(remote_ip: str) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((remote_ip, 9))
        return probe.getsockname()[0]
    except OSError:
        return socket.gethostbyname(socket.gethostname())
    finally:
        probe.close()
