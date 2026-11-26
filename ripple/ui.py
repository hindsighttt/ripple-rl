import os, json, mimetypes
from PySide6.QtCore import QUrl, QBuffer, QIODevice
from PySide6.QtGui import QIcon
from PySide6.QtWebEngineCore import (QWebEngineUrlScheme, QWebEngineUrlSchemeHandler,
                                     QWebEngineProfile, QWebEngineScript,
                                     QWebEngineUrlRequestJob)
from PySide6.QtWebEngineWidgets import QWebEngineView
from . import store

MIME = {".html": "text/html", ".js": "text/javascript", ".css": "text/css",
        ".png": "image/png", ".ico": "image/x-icon", ".json": "application/json",
        ".svg": "image/svg+xml", ".woff2": "font/woff2"}

# Cache of UI asset bytes keyed by absolute path -> (mtime_ns, size, bytes).
# app.js and app.css are ~550 KB each and were re-read from disk on every
# scheme request; serve them from memory and revalidate against mtime.
_asset_cache = {}


def _read_asset(full):
    try:
        st = os.stat(full)
    except OSError:
        return open(full, "rb").read()
    key = (st.st_mtime_ns, st.st_size)
    hit = _asset_cache.get(full)
    if hit is not None and hit[0] == key:
        return hit[1]
    data = open(full, "rb").read()
    _asset_cache[full] = (key, data)
    return data


def register_scheme():
    s = QWebEngineUrlScheme(b"ripple")
    s.setFlags(QWebEngineUrlScheme.Flag.SecureScheme |
               QWebEngineUrlScheme.Flag.LocalAccessAllowed |
               QWebEngineUrlScheme.Flag.FetchApiAllowed |
               QWebEngineUrlScheme.Flag.ContentSecurityPolicyIgnored)
    s.setSyntax(QWebEngineUrlScheme.Syntax.Host)
    QWebEngineUrlScheme.registerScheme(s)


class Handler(QWebEngineUrlSchemeHandler):
    def __init__(self, bridge, parent=None):
        super().__init__(parent)
        self.bridge = bridge

    def requestStarted(self, job):
        url = job.requestUrl()
        if url.path().startswith("/__bridge/"):
            raw = url.path()[len("/__bridge/"):]
            try:
                import urllib.parse
                msg = json.loads(urllib.parse.unquote(raw))
                self.bridge.dispatch(msg)
            except Exception as e:
                print("bridge error", e)
            buf = QBuffer(job)
            buf.setData(b"{}")
            buf.open(QIODevice.ReadOnly)
            job.reply(b"application/json", buf)
            return
        path = url.path().lstrip("/") or "index.html"
        if path.startswith("__packs/"):
            from . import packs
            parts = path.split("/")
            try:
                if len(parts) != 4 or parts[3] != "thumb.png":
                    raise ValueError("Invalid thumbnail path")
                full = str(packs.pack_path(parts[1], parts[2]) / "thumb.png")
            except ValueError:
                job.fail(QWebEngineUrlRequestJob.Error.UrlNotFound)
                return
        else:
            full = os.path.join(store.UI_DIR, path.replace("/", os.sep))
        if not os.path.isfile(full):
            job.fail(QWebEngineUrlRequestJob.Error.UrlNotFound)
            return
        data = _read_asset(full)
        buf = QBuffer(job)
        buf.setData(data)
        buf.open(QIODevice.ReadOnly)
        mime = MIME.get(os.path.splitext(full)[1].lower(),
                        mimetypes.guess_type(full)[0] or "application/octet-stream")
        job.reply(mime.encode(), buf)


SHIM = r"""
(function(){
  var post=function(m){
    var s=(typeof m==='string')?m:JSON.stringify(m);
    fetch('ripple://local/__bridge/'+encodeURIComponent(s)).catch(function(){});
  };
  window.chrome={webview:{postMessage:post,addEventListener:function(t,f){window.__mf=f}}};
  window.__emit=function(s){ try{ window.__mf && window.__mf({data:s}); }catch(e){} };
})();
"""


class WebView(QWebEngineView):
    def __init__(self, bridge, parent=None):
        super().__init__(parent)
        self.bridge = bridge
        profile = QWebEngineProfile.defaultProfile()
        self.handler = Handler(bridge, self)
        profile.installUrlSchemeHandler(b"ripple", self.handler)
        script = QWebEngineScript()
        script.setSourceCode(SHIM)
        script.setInjectionPoint(QWebEngineScript.InjectionPoint.DocumentCreation)
        script.setWorldId(QWebEngineScript.ScriptWorldId.MainWorld)
        script.setRunsOnSubFrames(True)
        self.page().scripts().insert(script)

    def emit(self, type_, payload):
        data = json.dumps({"type": type_, "payload": payload or {}})
        self.page().runJavaScript("window.__emit&&window.__emit(%s)" % json.dumps(data))

    def load_app(self):
        self.load(QUrl("ripple://local/index.html?v=%d" % int(__import__("time").time() * 1000)))


