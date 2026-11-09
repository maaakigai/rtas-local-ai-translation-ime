"""Exercise the real WinHTTP translation entry point against a loopback server.

Run with the path to a compiled llm_backend_probe.exe as the sole argument.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main():
    probe = str(Path(sys.argv[1]).resolve())
    requests = []
    reply = {"status": 200, "body": {
        "content": "Ignore this unrelated field",
        "choices": [{"message": {"content": "Hello 🌏"}}]}}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append((self.path, None))
            self.send_response(reply["status"])
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            payload = reply["body"]
            self.wfile.write((payload if isinstance(payload, str) else json.dumps(payload)).encode())

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            requests.append((self.path, body))
            self.send_response(reply["status"])
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            payload = reply["body"]
            self.wfile.write((payload if isinstance(payload, str) else json.dumps(payload)).encode())

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = {key: value for key, value in os.environ.items() if not key.startswith("IME3_OLLAMA_")}

    with tempfile.TemporaryDirectory() as directory:
        config_path = Path(directory) / "settings.json"

        def run(llm, *, cancelled=False, environment=None, legacy=False, models=False):
            requests.clear()
            provider = {"llm": llm} if legacy else {"translation": {"llm": llm}}
            config_path.write_text(json.dumps({"provider": provider}))
            args = [probe, str(config_path)] + (["models"] if models else ["cancel"] if cancelled else [])
            result = subprocess.run(args, env=environment or env, capture_output=True, check=True, timeout=10)
            return json.loads(result.stdout)

        settings = {"backend": "llama_cpp", "model": "rtas", "port": server.server_port}
        result = run(settings)
        assert result["success"], result
        assert result["translation"] == "Hello 🌏", result
        assert result["request_id"] == 42
        path, body = requests[-1]
        assert path == "/v1/chat/completions", path
        assert body["model"] == "rtas" and body["stream"] is False
        assert body["temperature"] == 0.2
        assert "こんにちは\"\n世界" in body["messages"][0]["content"]
        assert "直前の文章" in body["messages"][0]["content"]
        assert "options" not in body and "prompt" not in body

        # Legacy Ollama environment variables must not redirect llama.cpp traffic.
        result = run(settings, environment={**env, "IME3_OLLAMA_PORT": "1",
                                           "IME3_OLLAMA_HOST": "invalid.invalid",
                                           "IME3_OLLAMA_MODEL": "wrong"})
        assert result["success"] and requests[-1][1]["model"] == "rtas", result

        result = run({**settings, "path": "custom/chat", "model": "default"})
        assert result["success"] and requests[-1][0] == "/custom/chat", result
        assert requests[-1][1]["model"] == "rtas"

        result = run(settings, legacy=True)
        assert result["success"] and requests[-1][0] == "/v1/chat/completions", result

        result = run(settings, cancelled=True)
        assert result["cancelled"] and not requests, result

        for payload in ("not json", {}, {"choices": []}, {"choices": {}},
                        {"choices": [{"message": {"content": None}}]},
                        {"choices": [{"message": {"content": "  "}}]}):
            reply["body"] = payload
            result = run(settings)
            assert not result["success"] and result["error"], result

        reply.update(status=503, body={"choices": [{"message": {"content": "should fail"}}]})
        result = run(settings)
        assert not result["success"] and "503" in result["error"], result

        reply.update(status=200, body={"response": "Hello 🌏"})
        result = run({"model": "legacy", "port": server.server_port})
        assert result["success"] and result["translation"] == "Hello 🌏", result
        path, body = requests[-1]
        assert path == "/api/generate" and body["model"] == "legacy"
        assert body["options"]["temperature"] == 0.2 and body["stream"] is False

        reply["body"] = {"response": "override"}
        result = run({"model": "legacy", "port": server.server_port},
                     environment={**env, "IME3_OLLAMA_MODEL": "override-model"})
        assert result["success"] and requests[-1][1]["model"] == "override-model", result

        result = run({"backend": "llama_cpp"}, cancelled=True)
        assert result["port"] == 8080 and result["path"] == "/v1/chat/completions", result
        result = run({}, cancelled=True)
        assert result["port"] == 11434 and result["path"] == "/api/generate", result
        result = run({"backend": "unsupported"}, cancelled=True)
        assert "backend" in result["config_error"], result

        reply["body"] = {"data": [{"id": "plamo-3-translate"}, {"id": "custom"}, {"id": "custom"}]}
        result = run(settings, models=True)
        assert result["models"] == ["plamo-3-translate", "custom"], result
        assert requests[-1][0] == "/v1/models"
        reply["body"] = {"models": [{"name": "plamo:latest"}, {"name": "llama3.1:latest"}]}
        result = run({"port": server.server_port}, models=True)
        assert result["models"] == ["plamo:latest", "llama3.1:latest"], result
        assert requests[-1][0] == "/api/tags"
        reply["body"] = {"data": {}}
        result = run(settings, models=True)
        assert result["error"], result
        reply.update(status=503, body={"data": []})
        result = run(settings, models=True)
        assert "503" in result["error"], result
        reply["status"] = 200

        reply["body"] = {"choices": [{"text": "Hello 🌏"}]}
        result = run({**settings, "model": "plamo-2-translate"})
        assert result["success"], result
        path, body = requests[-1]
        assert path == "/v1/completions", path
        assert body["prompt"].startswith("<|plamo:op|>dataset\ntranslation\n")
        assert "<|plamo:op|>input lang=Japanese\nこんにちは" in body["prompt"]
        assert body["prompt"].endswith("<|plamo:op|>output lang=English\n")
        assert body["stop"] == ["<|plamo:op|>"] and body["temperature"] == 0
        result = run({**settings, "model": "alias", "prompt_format": "plamo3"})
        assert result["success"], result
        body = requests[-1][1]
        assert body["prompt"].startswith("<|plamo:tag|>dataset<|plamo:msg|>translation")
        assert "<|plamo:tag|>context<|plamo:msg|>直前の文章" in body["prompt"]
        assert body["prompt"].endswith("<|plamo:tag|>output<|plamo:key|>lang<|plamo:val|>en<|plamo:msg|>")
        assert body["stop"] == ["<|plamo:tag|>", "<|plamo:bos|>"]
        result = run({**settings, "model": "pfnet/plamo-3-translate-2b-GGUF"})
        assert result["success"] and requests[-1][0] == "/v1/completions", result
        reply["body"] = {"choices": [{"text": "incomplete", "finish_reason": "length"}]}
        result = run({**settings, "model": "plamo-3-translate"})
        assert not result["success"] and result["error"], result

        reply["body"] = {"choices": [{"message": {"content": "generic chat"}}]}
        result = run({**settings, "model": "plamo-3-alias", "prompt_format": "chat"})
        assert result["success"] and requests[-1][0] == "/v1/chat/completions", result

        reply["body"] = {"response": "Hello 🌏"}
        result = run({"port": server.server_port, "model": "plamo-3-translate"})
        assert result["success"], result
        assert requests[-1][1]["raw"] is True
        assert requests[-1][1]["options"]["temperature"] == 0
        reply["body"] = {"response": "incomplete", "done_reason": "length"}
        result = run({"port": server.server_port, "model": "plamo-2-translate"})
        assert not result["success"] and result["error"], result

    server.shutdown()
    print("PASS: llama.cpp and legacy Ollama requests, Unicode, errors, defaults, environment isolation, cancellation")


if __name__ == "__main__":
    main()
