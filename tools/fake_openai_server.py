#!/usr/bin/env python3
"""Tiny OpenAI-compatible fake server for end-to-end testing of rm-chat.

Serves on 0.0.0.0:8765 (the tablet reaches the host at 10.11.99.6 over USB).
Validates the request shape and answers like a vision model would, including
the Read: "..." transcription line the app's system prompt asks for.
"""
import base64
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("[fake-openai]", fmt % args)

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            if not self.headers.get("Authorization", "").startswith("Bearer "):
                self._json(401, {"error": {"message": "missing bearer token"}})
                return
            self._json(200, {"object": "list", "data": [{"id": "fake-vision-model"}]})
        else:
            self._json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self._json(404, {"error": {"message": "not found"}})
            return
        if not self.headers.get("Authorization", "").startswith("Bearer "):
            self._json(401, {"error": {"message": "missing bearer token"}})
            return

        length = int(self.headers.get("Content-Length", 0))
        try:
            req = json.loads(self.rfile.read(length))
        except json.JSONDecodeError:
            self._json(400, {"error": {"message": "bad json"}})
            return

        msgs = req.get("messages", [])
        image_bytes = 0
        got_system = any(m.get("role") == "system" for m in msgs)
        for m in msgs:
            content = m.get("content")
            if isinstance(content, list):
                for part in content:
                    if part.get("type") == "image_url":
                        url = part["image_url"]["url"]
                        if url.startswith("data:image/png;base64,"):
                            image_bytes = len(base64.b64decode(url.split(",", 1)[1]))

        detail = (f"model={req.get('model')} messages={len(msgs)} "
                  f"system={got_system} image_png_bytes={image_bytes}")
        print("[fake-openai] request:", detail)

        content = (
            'Read: "E2E test scribble"\n\n'
            f"Fake server received your message ({detail}). "
            "The whole pipeline works - ink was rendered, encoded and delivered."
        )
        self._json(200, {
            "id": "chatcmpl-fake", "object": "chat.completion",
            "model": req.get("model", "fake"),
            "choices": [{"index": 0, "finish_reason": "stop",
                         "message": {"role": "assistant", "content": content}}],
        })


if __name__ == "__main__":
    print("[fake-openai] listening on 0.0.0.0:8765")
    HTTPServer(("0.0.0.0", 8765), Handler).serve_forever()
