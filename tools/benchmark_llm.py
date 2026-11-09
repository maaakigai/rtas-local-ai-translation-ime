"""Compare local Ollama and llama-server translation latency and output.

Use matching model weights, quantization, context and hardware for comparison.
"""

import argparse
import json
import statistics
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ollama-model", required=True)
    parser.add_argument("--llama-model", default="rtas")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434/api/generate")
    parser.add_argument("--llama-url", default="http://127.0.0.1:8080/v1/chat/completions")
    parser.add_argument("--text", default="本日の会議は午後3時から始まります。")
    parser.add_argument("--runs", type=int, default=5, help="Measured runs after one initial request")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    if args.runs < 1 or args.timeout <= 0:
        parser.error("runs and timeout must be positive")

    prompt = ("Translate the following Japanese text into natural English. "
              "Reply with English only and avoid extra commentary.\n" + args.text.strip())
    endpoints = [
        ("ollama", args.ollama_url, {
            "model": args.ollama_model, "prompt": prompt, "stream": False,
            "options": {"temperature": 0.2}}),
        ("llama_cpp", args.llama_url, {
            "model": args.llama_model, "messages": [{"role": "user", "content": prompt}],
            "stream": False, "temperature": 0.2}),
    ]
    # Match RTAS WinHTTP's direct connection behavior.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    failed = False
    for backend, url, payload in endpoints:
        timings = []
        for index in range(args.runs + 1):
            start = time.perf_counter()
            try:
                request = urllib.request.Request(url, data=json.dumps(payload).encode(),
                                                 headers={"Content-Type": "application/json"})
                with opener.open(request, timeout=args.timeout) as response:
                    result = json.load(response)
                text = (result["response"] if backend == "ollama"
                        else result["choices"][0]["message"]["content"])
                if not isinstance(text, str) or not text.strip():
                    raise ValueError("Server returned an empty translation")
                elapsed = (time.perf_counter() - start) * 1000
                if index > 0:
                    timings.append(elapsed)
                print(json.dumps({"backend": backend, "phase": "initial" if index == 0 else "measured",
                                  "run": index, "elapsed_ms": round(elapsed, 1), "translation": text},
                                 ensure_ascii=False), flush=True)
            except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
                print(json.dumps({"backend": backend, "error": str(error)}, ensure_ascii=False), flush=True)
                failed = True
                break
        if timings:
            print(json.dumps({"backend": backend, "completed_runs": len(timings),
                              "median_ms": round(statistics.median(timings), 1)}), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
