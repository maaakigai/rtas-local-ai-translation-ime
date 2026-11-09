"""Check the local launcher against a simulated loading server.

Requires the local runtime and 0.8B model to have been installed.
"""

from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time
import tempfile


def main():
    repo = Path(__file__).resolve().parents[2]
    model = repo / "build/models/plamo-3-translate-0.8b-Q4_K_M.gguf"
    shell = shutil.which("pwsh") or shutil.which("powershell")
    if not shell or not model.is_file():
        print("SKIP: PowerShell and the installed 0.8B model are required")
        return

    ready_at = time.monotonic() + 3
    model_path = str(model)
    temporary = tempfile.TemporaryDirectory()
    config_path = Path(temporary.name) / "settings.json"
    config_path.write_text('{"provider":{"translation":{"llm":{}}}}')

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            loading = self.path == "/health" and time.monotonic() < ready_at
            self.send_response(503 if loading else 200)
            self.end_headers()
            responses = {
                "/health": {"status": "loading" if loading else "ok"},
                "/v1/models": {"data": [{"id": "plamo-3-translate"}]},
                "/props": {"model_path": model_path},
            }
            self.wfile.write(json.dumps(responses[self.path]).encode())

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def launch(*extra):
        return subprocess.run(
            [shell, "-NoProfile", "-File", str(repo / "tools/start_plamo.ps1"),
             "-Port", str(server.server_port), "-ConfigPath", str(config_path), *extra],
            capture_output=True, timeout=30,
        )

    try:
        with ThreadPoolExecutor(max_workers=2) as workers:
            results = list(workers.map(lambda _index: launch(), range(2)))
        for result in results:
            assert result.returncode == 0, (result.stdout, result.stderr)
        status_path = Path(temporary.name) / "result.json"
        result = launch("-Device", "cpu", "-Restart", "-ResultPath", str(status_path))
        assert result.returncode != 0, "Unmanaged server must not be restarted"
        assert json.loads(status_path.read_text(encoding="utf-8-sig"))["success"] is False
        model_path = str(repo / "build/models/different-model.gguf")
        result = launch()
        assert result.returncode != 0, "A different loaded model must be rejected"
    finally:
        server.shutdown()
        server.server_close()
        temporary.cleanup()
    print("PASS: concurrent startup waits for loading server, different loaded model rejected")


if __name__ == "__main__":
    main()
