# reconstruct_colmap.py
# Sparse-only COLMAP pipeline (CPU-friendly) for downscaled images.

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def find_colmap() -> str:
    # 1) normal PATH
    p = shutil.which("colmap")
    if p:
        return p

    # 2) common conda location on Windows
    conda_prefix = os.environ.get("CONDA_PREFIX")
    if conda_prefix:
        candidate = os.path.join(conda_prefix, "Library", "bin", "colmap.exe")
        if os.path.exists(candidate):
            return candidate

    raise FileNotFoundError(
        "COLMAP executable not found.\n"
        "Try:\n"
        "  conda install -c conda-forge colmap -y\n"
        "Then verify with:\n"
        "  colmap -h\n"
    )


COLMAP = find_colmap()


def run(cmd: list[str]) -> None:
    # Replace "colmap" with absolute path for reliability on Windows/conda
    if cmd and cmd[0] == "colmap":
        cmd = [COLMAP] + cmd[1:]
    print("\n>>>", " ".join(map(str, cmd)))
    subprocess.run(cmd, check=True)


def pick_model_folder(sparse_dir: Path) -> Path:
    """Pick sparse/0 if it exists; otherwise first numeric model folder."""
    if (sparse_dir / "0").is_dir():
        return sparse_dir / "0"
    candidates = [p for p in sparse_dir.iterdir() if p.is_dir() and p.name.isdigit()]
    if not candidates:
        raise FileNotFoundError(f"No sparse model folders found in: {sparse_dir}")
    return sorted(candidates, key=lambda p: int(p.name))[0]


def reconstruct_sparse_only(
    images_dir: Path,
    out_dir: Path,
    matcher: str = "sequential",   # "sequential" | "exhaustive"
    single_camera: bool = True,
    num_threads: int = 4,
    max_num_features: int = 4096,
    sequential_overlap: int = 8,
) -> None:
    images_dir = images_dir.resolve()
    out_dir = out_dir.resolve()

    if not images_dir.exists():
        raise FileNotFoundError(f"Images folder not found: {images_dir}")

    out_dir.mkdir(parents=True, exist_ok=True)
    db_path = out_dir / "database.db"
    sparse_dir = out_dir / "sparse"
    sparse_dir.mkdir(exist_ok=True)

    # 0) (Optional) If you re-run often, start fresh to avoid mixing settings
    # Comment these two lines out if you want to resume.
    if db_path.exists():
        db_path.unlink()

    # 1) Feature extraction (CPU-only, lighter settings)
    run([
        "colmap", "feature_extractor",
        "--database_path", str(db_path),
        "--image_path", str(images_dir),

        "--ImageReader.single_camera", "1" if single_camera else "0",

        # CPU-only (you have no NVIDIA GPU)
        "--FeatureExtraction.use_gpu", "0",

        # Keep your PC responsive
        "--FeatureExtraction.num_threads", str(num_threads),

        # Limit features to speed up (good for 353 building images)
        "--SiftExtraction.max_num_features", str(max_num_features),
    ])

    # 2) Matching (CPU-only)
    if matcher == "sequential":
        run([
            "colmap", "sequential_matcher",
            "--database_path", str(db_path),

            "--SequentialMatching.overlap", str(sequential_overlap),

            "--FeatureMatching.use_gpu", "0",
            "--FeatureMatching.num_threads", str(num_threads),
        ])
    elif matcher == "exhaustive":
        run([
            "colmap", "exhaustive_matcher",
            "--database_path", str(db_path),

            "--FeatureMatching.use_gpu", "0",
            "--FeatureMatching.num_threads", str(num_threads),
        ])
    else:
        raise ValueError("matcher must be 'sequential' or 'exhaustive'")

    # 3) Sparse reconstruction (SfM)
    run([
        "colmap", "mapper",
        "--database_path", str(db_path),
        "--image_path", str(images_dir),
        "--output_path", str(sparse_dir),
    ])

    model_dir = pick_model_folder(sparse_dir)
    print(f"\n✅ Sparse model created at: {model_dir}")
    print("You should see cameras.bin / images.bin / points3D.bin in that folder.")


def main():
    parser = argparse.ArgumentParser(description="Sparse-only COLMAP pipeline (CPU-friendly).")
    parser.add_argument("--images", type=str, required=True, help="Folder of (downscaled) images.")
    parser.add_argument("--out", type=str, default="recon", help="Output folder (default: recon).")
    parser.add_argument("--matcher", type=str, default="sequential", choices=["sequential", "exhaustive"])
    parser.add_argument("--multi_camera", action="store_true",
                        help="Set if images come from multiple cameras/lenses (disables single_camera).")

    # Performance knobs (defaults are good for your dataset)
    parser.add_argument("--threads", type=int, default=4, help="CPU threads to use (2–6 recommended).")
    parser.add_argument("--max_features", type=int, default=4096, help="Max SIFT features per image.")
    parser.add_argument("--overlap", type=int, default=8, help="Sequential overlap (6–10 recommended).")

    args = parser.parse_args()

    reconstruct_sparse_only(
        images_dir=Path(args.images),
        out_dir=Path(args.out),
        matcher=args.matcher,
        single_camera=not args.multi_camera,
        num_threads=args.threads,
        max_num_features=args.max_features,
        sequential_overlap=args.overlap,
    )


if __name__ == "__main__":
    main()
