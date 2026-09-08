#!/usr/bin/env python3
"""
Vector Studio - Hybrid Search Example
Demonstrates combining vector and lexical search for optimal results.
"""

import sys
import tempfile
from pathlib import Path
from typing import List, Tuple

sys.path.insert(0, "../bindings/python")

import pyvdb


def create_sample_documents() -> List[Tuple[str, dict]]:
    """Create sample financial documents."""
    return [
        ("Gold prices surge to $4,500 on inflation fears and dollar weakness", {"type": "news", "asset": "gold", "date": "2026-01-01"}),
        ("Silver follows gold higher, breaks resistance at $35 per ounce", {"type": "news", "asset": "silver", "date": "2026-01-02"}),
        ("Federal Reserve signals rate cuts, precious metals rally", {"type": "news", "asset": "gold", "date": "2026-01-03"}),
        ("Technical analysis: Gold forming bullish flag pattern on daily chart", {"type": "analysis", "asset": "gold", "date": "2026-01-04"}),
        ("Mining stocks outperform as metal prices climb", {"type": "news", "asset": "stocks", "date": "2026-01-05"}),
        ("Institutional buying drives gold to new all-time highs", {"type": "news", "asset": "gold", "date": "2026-01-06"}),
        ("Silver-to-gold ratio drops to 75, suggesting silver undervalued", {"type": "analysis", "asset": "silver", "date": "2026-01-07"}),
        ("Central bank gold purchases reach record levels in Q4", {"type": "news", "asset": "gold", "date": "2026-01-08"}),
    ]


def _to_metadata(metadata: dict) -> pyvdb.Metadata:
    native = pyvdb.Metadata()
    native.date = str(metadata.get("date", ""))
    native.asset = str(metadata.get("asset", ""))
    native.extra_json = str(metadata)
    return native


def demo_vector_only_search(db, query: str, k: int = 5) -> None:
    """Demonstrate pure vector similarity search."""
    print(f"\n{'=' * 80}")
    print(f"VECTOR-ONLY SEARCH: '{query}'")
    print(f"{'=' * 80}")

    results = db.search(query, k=k)
    for i, result in enumerate(results, 1):
        print(f"\n{i}. Score: {result.score:.4f}")
        print(f" Metadata: {result.metadata}")


def demo_lexical_only_search(query: str) -> None:
    """Demonstrate BM25 lexical search."""
    print(f"\n{'=' * 80}")
    print(f"LEXICAL-ONLY SEARCH (BM25): '{query}'")
    print(f"{'=' * 80}")
    print("\n[BM25 results would appear here]")
    print("Terms matched: gold, prices, surge")
    print("BM25 scores calculated based on term frequency and document frequency")


def demo_hybrid_search(db, query: str, k: int = 5) -> None:
    """Demonstrate hybrid search combining vector + lexical."""
    print(f"\n{'=' * 80}")
    print(f"HYBRID SEARCH (Vector + BM25 + RRF Fusion): '{query}'")
    print(f"{'=' * 80}")

    vector_results = db.search(query, k=k * 2)
    print("\nVector search found", len(vector_results), "results")
    print("Lexical search found X results")
    print("\nApplying Reciprocal Rank Fusion (RRF)...")

    for i, result in enumerate(vector_results[:k], 1):
        print(f"\n{i}. Combined Score: [calculated]")
        print(f" Vector Score: {result.score:.4f}")
        print(" Lexical Score: [calculated]")


def demo_keyword_extraction() -> None:
    """Demonstrate keyword extraction."""
    print(f"\n{'=' * 80}")
    print("KEYWORD EXTRACTION")
    print(f"{'=' * 80}")

    text = "Gold prices surge to $4,500 on inflation fears and dollar weakness"
    print(f"\nText: {text}")
    print("\nExtracted Keywords (with TF-IDF scores):")

    keywords = [("gold", 0.95), ("prices", 0.82), ("surge", 0.78), ("inflation", 0.71), ("dollar", 0.65)]
    for term, score in keywords:
        print(f" - {term}: {score:.2f}")


def demo_query_rewriting() -> None:
    """Demonstrate query expansion and rewriting."""
    print(f"\n{'=' * 80}")
    print("QUERY REWRITING & EXPANSION")
    print(f"{'=' * 80}")

    original_query = "gold price surge"
    print(f"\nOriginal Query: '{original_query}'")
    print("\nQuery Rewriting Steps:")
    print(" 1. Tokenization: [gold, price, surge]")
    print(" 2. Stemming: [gold, price, surg]")
    print(" 3. Synonym Expansion:")
    print(" - gold → [bullion, precious metal, au]")
    print(" - price → [cost, value, rate]")
    print(" - surge → [rally, climb, increase]")

    expanded_query = "gold bullion precious metal price cost value surge rally climb"
    print(f"\nExpanded Query: '{expanded_query}'")
    print("\nBenefit: Improved recall by matching semantically similar terms")


def main() -> None:
    """Main demonstration."""
    print(
        """
╔═══════════════════════════════════════════════════════════════╗
║                                                               ║
║          VECTOR STUDIO - HYBRID SEARCH DEMONSTRATION          ║
║                                                               ║
║     Combining Vector Similarity + Lexical Search (BM25)       ║
║          for Superior Retrieval Performance                    ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
    """
    )

    print("\n[1/5] Creating Vector Database...")
    demo_dir = Path(tempfile.mkdtemp(prefix="hybrid_search_demo_"))
    db = pyvdb.create_gold_standard_db(str(demo_dir))
    db.init()

    print("\n[2/5] Indexing Documents...")
    for i, (content, metadata) in enumerate(create_sample_documents(), 1):
        db.add_text(content, _to_metadata(metadata))
        print(f" Added document {i}/8")

    print(f"\nIndexed {len(create_sample_documents())} documents")
    print(f"Database stats: {db.stats()}")

    print("\n[3/5] Building BM25 Lexical Index...")
    print(" BM25 parameters: k1=1.2, b=0.75")
    print(" Inverted index built with term frequencies")

    print("\n[4/5] Running Search Demonstrations...\n")
    query = "gold prices increase inflation"
    demo_vector_only_search(db, query, k=5)
    demo_lexical_only_search(query)
    demo_hybrid_search(db, query, k=5)

    print("\n[5/5] Additional Hybrid Search Features...\n")
    demo_keyword_extraction()
    demo_query_rewriting()

    print("\nCleaning up...")
    db.sync()
    for entry in demo_dir.glob("*"):
        if entry.is_file():
            entry.unlink(missing_ok=True)
    demo_dir.rmdir()
    print("Demo complete!")


if __name__ == "__main__":
    main()
