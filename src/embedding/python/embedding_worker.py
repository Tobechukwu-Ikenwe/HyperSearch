#!/usr/bin/env python3
"""Persistent JSONL embedding worker for HyperSearch."""

import json
import os
import sys
import traceback

from sentence_transformers import SentenceTransformer


MODEL_ID = os.environ.get(
    "HYPERSEARCH_EMBEDDING_MODEL",
    "sentence-transformers/all-MiniLM-L6-v2",
)


def log(message: str) -> None:
    """Keep diagnostics off stdout so stdout remains valid JSONL."""
    print(message, file=sys.stderr, flush=True)


def load_model():
    """Load the model once and reuse it for every request."""
    log(f"Loading embedding model: {MODEL_ID}")
    model = SentenceTransformer(MODEL_ID)
    dimension = model.get_sentence_embedding_dimension()

    if dimension is None or dimension <= 0:
        raise RuntimeError("Model did not report a valid embedding dimension")

    log(f"Embedding model ready; dimension={dimension}")
    return model, int(dimension)


def embed_texts(model, texts: list[str]) -> list[list[float]]:
    """Embed a batch and return normalized, JSON-compatible vectors."""
    if not all(isinstance(text, str) for text in texts):
        raise ValueError("Every item in texts must be a string")

    if not texts:
        return []

    vectors = model.encode(
        texts,
        batch_size=32,
        convert_to_numpy=True,
        normalize_embeddings=True,
        show_progress_bar=False,
    )

    # Convert NumPy values into ordinary Python floats for JSON serialization.
    return [
        [float(value) for value in vector]
        for vector in vectors
    ]


def handle_request(model, dimension: int, request: dict) -> dict:
    """Dispatch one request and construct its response."""
    request_id = request.get("id")
    operation = request.get("op")

    response = {
        "id": request_id,
        "model_id": MODEL_ID,
        "dimension": dimension,
    }

    if operation == "metadata":
        response.update({
            "ok": True,
        })

    elif operation == "embed_documents":
        texts = request.get("texts")
        if not isinstance(texts, list):
            raise ValueError("'texts' must be a JSON array")

        embeddings = embed_texts(model, texts)

        response.update({
            "ok": True,
            "embeddings": embeddings,
        })

    elif operation == "embed_query":
        text = request.get("text")
        if not isinstance(text, str):
            raise ValueError("'text' must be a string")

        embeddings = embed_texts(model, [text])

        response.update({
            "ok": True,
            "embedding": embeddings[0],
        })

    else:
        raise ValueError(
            "Unsupported operation; expected metadata, "
            "embed_documents, or embed_query"
        )

    return response


def main() -> int:
    try:
        model, dimension = load_model()
    except Exception as error:
        log(f"Could not initialize embedding model: {error}")
        return 1

    # One input line produces one output line; the process remains alive.
    for line_number, line in enumerate(sys.stdin, start=1):
        if not line.strip():
            continue

        request_id = None

        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("Each request must be a JSON object")

            request_id = request.get("id")
            response = handle_request(model, dimension, request)

        except Exception as error:
            response = {
                "id": request_id,
                "ok": False,
                "error": str(error),
            }
            log(f"Request on input line {line_number} failed: {error}")

        try:
            print(
                json.dumps(response, allow_nan=False),
                flush=True,
            )
        except (TypeError, ValueError) as error:
            log(f"Could not serialize response: {error}")
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
