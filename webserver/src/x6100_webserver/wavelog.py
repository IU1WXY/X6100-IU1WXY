"""Wavelog live upload.

The radio's GUI writes every logged QSO (FT8, FT4, JS8 ...) into the SQLite
database settings.QSO_LOG_DB. A background thread of this web server watches
that table and sends each new row to Wavelog (classic API: POST
<url>/index.php/api/qso, ADIF inside JSON). The GUI is not touched.

Queue: the id (rowid) of the last QSO that Wavelog has answered for is kept in
state.json. Everything after it is still to do, so a Wi-Fi outage or a reboot
loses nothing. Wavelog does the duplicate check itself.

Settings and state live in /mnt/.wavelog (the API key must not be reachable
through the file browser: apps.files() hides this directory).
"""
import json
import logging
import os
import re
import sqlite3
import threading
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

try:
    import ssl
except ImportError:   # python built without SSL
    ssl = None

import bottle

from . import settings

DATA_DIR = "/mnt/.wavelog"
POLL_IDLE = 5       # seconds between looks at the log when nothing is to do
POLL_RETRY = 30     # seconds before trying again after a failure
BATCH = 20          # QSOs per round
TIMEOUT = 8         # seconds per request

DEFAULT_CONF = {"enabled": False, "url": "", "key": "", "station_id": "", "insecure_tls": False}
DEFAULT_STATE = {"last_id": None, "uploaded": 0, "dupes": 0, "skipped": 0,
                 "last_ok": "", "last_error": "", "last_response": "", "rejected": []}

# qso_log_mode_t of the GUI (src/qso_log.h) -> (ADIF MODE, SUBMODE)
MODES = {1: ("SSB", None), 2: ("AM", None), 3: ("FM", None), 4: ("CW", None), 5: ("FT8", None),
         6: ("MFSK", "FT4"), 7: ("RTTY", None), 8: ("MFSK", "JS8"), 9: ("MFSK", "FT2")}

BANDS = [(1.8, 2.0, "160m"), (3.5, 4.0, "80m"), (5.3515, 5.3665, "60m"), (7.0, 7.3, "40m"),
         (10.1, 10.15, "30m"), (14.0, 14.35, "20m"), (18.068, 18.168, "17m"), (21.0, 21.45, "15m"),
         (24.89, 24.99, "12m"), (28.0, 29.7, "10m"), (50.0, 54.0, "6m")]

_lock = threading.RLock()
_wake = threading.Event()
_started = False
_state = None


# ---------------------------------------------------------------- files

def _path(name):
    return os.path.join(DATA_DIR, name)


def _read_json(name, default):
    out = dict(default)
    try:
        with open(_path(name), encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            out.update(data)
    except (OSError, ValueError):
        pass
    return out


def _write_json(name, data):
    os.makedirs(DATA_DIR, mode=0o700, exist_ok=True)
    tmp = _path(name + ".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, _path(name))


def _config():
    return _read_json("config.json", DEFAULT_CONF)


def _config_ok(conf):
    return bool(conf["enabled"] and conf["url"] and conf["key"] and conf["station_id"])


def _state_get():
    global _state
    with _lock:
        if _state is None:
            _state = _read_json("state.json", DEFAULT_STATE)
        return _state


def _state_save():
    with _lock:
        try:
            _write_json("state.json", _state_get())
        except OSError as e:
            logging.error("Wavelog: can't save state (%s)", e)


def _now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def clean_url(url):
    url = (url or "").strip().rstrip("/")
    if url and not re.match(r'^https?://', url, re.I):
        url = "http://" + url
    if url.lower().endswith("/index.php"):
        url = url[:-len("/index.php")].rstrip("/")
    return url


# ---------------------------------------------------------------- QSO log

def _connect():
    uri = "file:%s?mode=ro" % urllib.parse.quote(settings.QSO_LOG_DB)
    return sqlite3.connect(uri, uri=True, timeout=5)


def max_rowid():
    try:
        con = _connect()
        try:
            return con.execute("SELECT COALESCE(MAX(rowid), 0) FROM qso_log").fetchone()[0]
        finally:
            con.close()
    except sqlite3.Error:
        return 0


def _clean(value):
    s = unicodedata.normalize("NFKD", str(value if value is not None else "")).encode("ascii", "ignore").decode()
    return re.sub(r'[\x00-\x1f<>]', " ", s).strip()


def _field(name, value):
    value = _clean(value)
    return "<%s:%d>%s" % (name, len(value), value) if value else ""


def band_of(freq_mhz):
    for lo, hi, name in BANDS:
        if lo <= freq_mhz <= hi:
            return name
    return ""


def build_adif(row):
    """(adif, None) for a qso_log row, or (None, reason) when it can't be sent."""
    _id, ts, freq, mode, _local, remote, rsts, rstr, lgrid, rgrid, name = row
    spec = MODES.get(mode)
    if not spec:
        return None, "mode %s has no ADIF equivalent" % mode
    try:
        dt = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
    except (TypeError, ValueError):
        return None, "bad time %r" % (ts,)
    remote = _clean(remote).upper()
    if not remote:
        return None, "no callsign"
    freq = round(float(freq or 0), 4)      # the GUI stores a 32 bit float
    adif_mode, submode = spec
    if adif_mode == "SSB":
        submode = "LSB" if freq < 10 and not 5.0 < freq < 6.0 else "USB"
    digital = mode in (5, 6, 8, 9)
    parts = [
        _field("CALL", remote),
        _field("QSO_DATE", dt.strftime("%Y%m%d")),
        _field("TIME_ON", dt.strftime("%H%M%S")),
        _field("TIME_OFF", dt.strftime("%H%M%S")),
        _field("BAND", band_of(freq)),
        _field("FREQ", "%.4f" % freq if freq > 0 else ""),
        _field("MODE", adif_mode),
        _field("SUBMODE", submode),
        _field("RST_SENT", rsts if (rsts or digital) else ""),
        _field("RST_RCVD", rstr if (rstr or digital) else ""),
        _field("GRIDSQUARE", (rgrid or "").upper()),
        _field("MY_GRIDSQUARE", (lgrid or "").upper()),
        _field("NAME", name),
    ]
    return "".join(parts) + "<EOR>", None


# ---------------------------------------------------------------- Wavelog

class TlsMissing(Exception):
    pass


def _open(req, conf):
    kw = {"timeout": TIMEOUT}
    if req.full_url.lower().startswith("https"):
        if ssl is None:
            raise TlsMissing()
        kw["context"] = ssl._create_unverified_context() if conf.get("insecure_tls") else ssl.create_default_context()
    return urllib.request.urlopen(req, **kw)


def _http(req, conf):
    """(status, body) or raises OSError-like for a missing connection."""
    try:
        with _open(req, conf) as resp:
            return resp.status, resp.read(65536).decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        try:
            body = e.read(65536).decode("utf-8", "replace")
        except (OSError, AttributeError):
            body = ""
        return e.code, body


def classify(status, text):
    """(kind, message, raw): kind is ok | dupe | rejected | auth | retry."""
    raw = text.strip()[:300]
    if status in (401, 403):
        return "auth", "API key rejected (HTTP %d)" % status, raw
    if status >= 500:
        return "retry", "Wavelog server error (HTTP %d)" % status, raw
    try:
        data = json.loads(text)
    except ValueError:
        data = None
    if not isinstance(data, dict):
        return "retry", "unexpected answer (HTTP %d), check the URL" % status, raw
    st = str(data.get("status", "")).lower()
    errors = data.get("adif_errors") or 0
    if st in ("failed", "abort", "error") or errors or status >= 400:
        why = data.get("reason") or data.get("info") or data.get("message") or raw
        if isinstance(why, (list, tuple)):
            why = "; ".join(str(w) for w in why)
        low = text.lower()
        why_low = str(why).lower()
        if "dupe" in low or "duplicate" in low:
            return "dupe", "duplicate", raw
        if "key" in why_low and any(w in why_low for w in ("invalid", "wrong", "missing", "auth")):
            return "auth", "API key rejected: %s" % why, raw
        return "rejected", str(why)[:200], raw
    return "ok", "", raw


def post_qso(conf, adif):
    url = conf["url"] + "/index.php/api/qso"
    body = json.dumps({"key": conf["key"], "station_profile_id": str(conf["station_id"]),
                       "type": "adif", "string": adif}).encode()
    req = urllib.request.Request(url, data=body, method="POST", headers={
        "Content-Type": "application/json", "Accept": "application/json", "User-Agent": "x6100-wavelog/1.0"})
    try:
        status, text = _http(req, conf)
    except TlsMissing:
        return "retry", "https:// needs Python with ssl, use http:// on the home network", ""
    except (urllib.error.URLError, OSError, ValueError) as e:
        return "retry", "no connection: %s" % getattr(e, "reason", e), ""
    return classify(status, text)


# ---------------------------------------------------------------- uploader

def _set_error(msg, raw=""):
    st = _state_get()
    with _lock:
        st["last_error"] = msg
        if raw:
            st["last_response"] = raw
    _state_save()


def _tick():
    """One round; returns the seconds to wait."""
    conf = _config()
    st = _state_get()
    if not _config_ok(conf):
        return POLL_IDLE
    if st["last_id"] is None:     # first time: start with the next QSO, not the whole log
        with _lock:
            st["last_id"] = max_rowid()
        _state_save()
    try:
        con = _connect()
        try:
            top = con.execute("SELECT COALESCE(MAX(rowid), 0) FROM qso_log").fetchone()[0]
            if top < st["last_id"]:       # the log was emptied or renumbered
                with _lock:
                    st["last_id"] = top
                _state_save()
            rows = con.execute(
                "SELECT rowid, ts, freq, mode, local_callsign, remote_callsign, rsts, rstr, "
                "local_grid, remote_grid, op_name FROM qso_log WHERE rowid > ? ORDER BY rowid LIMIT ?",
                (st["last_id"], BATCH)).fetchall()
        finally:
            con.close()
    except sqlite3.Error as e:
        _set_error("can't read the QSO log: %s" % e)
        return POLL_RETRY

    for row in rows:
        adif, why = build_adif(row)
        if adif is None:
            kind, msg, raw = "skipped", why, ""
        else:
            kind, msg, raw = post_qso(conf, adif)
        with _lock:
            if kind in ("retry", "auth"):
                st["last_error"] = msg
                st["last_response"] = raw or st["last_response"]
                _state_save()
                return POLL_RETRY
            if kind == "ok":
                st["uploaded"] += 1
            elif kind == "dupe":
                st["dupes"] += 1
            elif kind == "skipped":
                st["skipped"] += 1
            else:       # rejected for good: note it and go on, it must not block the queue
                st["rejected"] = ([{"call": row[5], "time": row[1], "why": msg}] + st["rejected"])[:20]
            if kind in ("ok", "dupe"):
                st["last_ok"] = _now()
                st["last_error"] = ""
            elif kind == "rejected":
                st["last_error"] = "Wavelog rejected %s: %s" % (row[5], msg)
            if raw:
                st["last_response"] = raw
            st["last_id"] = row[0]
            _state_save()
    return 1 if len(rows) == BATCH else POLL_IDLE


def _loop():
    while True:
        try:
            delay = _tick()
        except Exception:   # the web server must live on whatever happens here
            logging.exception("Wavelog uploader")
            delay = POLL_RETRY
        _wake.wait(delay)
        _wake.clear()


def start():
    global _started
    if _started:
        return
    _started = True
    threading.Thread(target=_loop, daemon=True, name="wavelog").start()


# ---------------------------------------------------------------- web

def public():
    conf = _config()
    st = _state_get()
    with _lock:
        last = st["last_id"]
        out = {k: st[k] for k in ("uploaded", "dupes", "skipped", "last_ok", "last_error", "last_response",
                                   "rejected")}
    out.update(enabled=conf["enabled"], url=conf["url"], station_id=conf["station_id"],
               insecure_tls=conf["insecure_tls"], has_key=bool(conf["key"]),
               pending=max(0, max_rowid() - last) if last is not None else 0)
    return out


def _error(code, msg):
    bottle.response.status = code
    return {"status": "error", "msg": msg}


def _json_body():
    try:
        data = bottle.request.json
    except ValueError:
        data = None
    return data if isinstance(data, dict) else {}


def register(app):
    @app.route('/wavelog')
    def wavelog_page():
        return bottle.template('wavelog')

    @app.get('/api/wavelog')
    def wavelog_get():
        return public()

    @app.post('/api/wavelog')
    def wavelog_save():
        data = _json_body()
        old = _config()
        url = clean_url(data.get("url"))
        key = str(data.get("key") or "").strip() or old["key"]
        sid = str(data.get("station_id") or "").strip()
        enabled = bool(data.get("enabled"))
        if url and not re.fullmatch(r'https?://[^\s/]+(/\S*)?', url):
            return _error(400, "The URL looks wrong, e.g. http://192.168.1.20/wavelog")
        if key and not re.fullmatch(r'[\w.\-]{4,128}', key):
            return _error(400, "The API key has unexpected characters")
        if sid and not re.fullmatch(r'\d{1,6}', sid):
            return _error(400, "The station profile ID is a number")
        if enabled and not (url and key and sid):
            return _error(400, "URL, API key and station profile ID are needed to enable the upload")
        if enabled and url.lower().startswith("https") and ssl is None:
            return _error(400, "This radio's Python has no ssl: use an http:// URL on your home network")
        was_on = _config_ok(old)
        with _lock:
            _write_json("config.json", {"enabled": enabled, "url": url, "key": key, "station_id": sid,
                                        "insecure_tls": bool(data.get("insecure_tls"))})
            if enabled and not was_on:       # switching on: from the next QSO on
                _state_get()["last_id"] = max_rowid()
                _state_get()["last_error"] = ""
                _state_save()
        _wake.set()
        return {"status": "OK", **public()}

    @app.post('/api/wavelog/test')
    def wavelog_test():
        data = _json_body()
        conf = _config()
        url = clean_url(data.get("url")) or conf["url"]
        key = str(data.get("key") or "").strip() or conf["key"]
        conf = dict(conf, insecure_tls=bool(data.get("insecure_tls", conf["insecure_tls"])))
        if not url or not key:
            return _error(400, "URL and API key are needed")
        req = urllib.request.Request(
            "%s/index.php/api/station_info/%s" % (url, urllib.parse.quote(key, safe="")),
            headers={"Accept": "application/json", "User-Agent": "x6100-wavelog/1.0"})
        try:
            status, text = _http(req, conf)
        except TlsMissing:
            return _error(400, "https:// needs Python with ssl, use http:// on the home network")
        except (urllib.error.URLError, OSError, ValueError) as e:
            return _error(502, "no connection: %s" % getattr(e, "reason", e))
        if status in (401, 403) or "auth error" in text.lower():
            return _error(401, "API key rejected")
        try:
            parsed = json.loads(text)
        except ValueError:
            parsed = None
        if status >= 400 or not isinstance(parsed, list):
            return _error(502, "unexpected answer (HTTP %d), check the URL" % status)
        stations = [{"id": str(s.get("station_id", "")), "name": s.get("station_profile_name", ""),
                     "call": s.get("station_callsign", "")} for s in parsed if isinstance(s, dict)]
        return {"status": "OK", "stations": stations}

    @app.post('/api/wavelog/resend')
    def wavelog_resend():
        """Send the last n QSOs of the log again (Wavelog skips the ones it has)."""
        if not _config_ok(_config()):
            return _error(400, "Enable the upload first")
        try:
            n = int(_json_body().get("n", 0))
        except (TypeError, ValueError):
            n = 0
        if not 1 <= n <= 5000:
            return _error(400, "Give a number between 1 and 5000")
        with _lock:
            _state_get()["last_id"] = max(0, max_rowid() - n)
            _state_save()
        _wake.set()
        return {"status": "OK", **public()}
