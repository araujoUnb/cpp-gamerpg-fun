"""Sends each test situation to a running llama-server and records the answer.

Usage: python3 avalia.py <server url> <data dir> <situations.json> <out.jsonl>
Standard library only, so it runs on lab10 without installing anything.
"""
import json
import sys
import time
import urllib.request

url, data_dir, situations_file, out_file = sys.argv[1:5]
master = json.load(open(f"{data_dir}/narrator/mestre.json", encoding="utf-8"))
schema = json.load(open(f"{data_dir}/narrator/resposta.schema.json", encoding="utf-8"))
situations = json.load(open(situations_file, encoding="utf-8"))

with open(out_file, "w", encoding="utf-8") as out:
    for s in situations:
        size = master["tamanho"]["combate" if s["momento"] == "combate" else "cena"]
        user = f"Situação (tamanho da narração: {size}):\n" + json.dumps(s, ensure_ascii=False, indent=1)
        body = {
            "messages": [{"role": "system", "content": master["sistema"]}, {"role": "user", "content": user}],
            "response_format": {"type": "json_schema", "json_schema": {"name": "resposta", "schema": schema}},
            "chat_template_kwargs": {"enable_thinking": False},
            "temperature": 0.8,
            "max_tokens": 700,
        }
        req = urllib.request.Request(f"{url}/v1/chat/completions", data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"})
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=600) as r:
            reply = json.load(r)
        secs = time.time() - t0
        text = reply["choices"][0]["message"]["content"]
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            parsed = None
        record = {"id": s["id"], "segundos": round(secs, 1), "tokens": reply.get("usage", {}),
                  "json_valido": parsed is not None, "resposta": parsed if parsed is not None else text}
        out.write(json.dumps(record, ensure_ascii=False) + "\n")
        out.flush()
        print(s["id"], f"{secs:.1f}s", reply.get("usage", {}), flush=True)
