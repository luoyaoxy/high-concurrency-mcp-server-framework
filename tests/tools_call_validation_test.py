#!/usr/bin/env python3

import json
import subprocess
import sys


server_path = sys.argv[1]
config_path = sys.argv[2]
requests = [
    {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"arguments": {}},
    },
    {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {"name": 7, "arguments": {}},
    },
]

process = subprocess.run(
    [server_path, "--mode", "stdio", "--config", config_path],
    input="".join(json.dumps(request) + "\n" for request in requests),
    capture_output=True,
    check=False,
    text=True,
    timeout=10,
)

if process.returncode != 0:
    raise AssertionError(
        f"server exited with {process.returncode}: {process.stderr}"
    )

responses = [json.loads(line) for line in process.stdout.splitlines()]
responses_by_id = {response["id"]: response for response in responses}
if set(responses_by_id) != {1, 2}:
    raise AssertionError(f"unexpected responses: {responses}")

for request_id in (1, 2):
    response = responses_by_id[request_id]
    error = response.get("error")
    if error is None or error.get("code") != -32602:
        raise AssertionError(
            f"request {request_id} did not return Invalid Params: {response}"
        )
