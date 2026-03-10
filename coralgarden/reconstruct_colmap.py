# reconstruct_colmap.py
# COLMAP sparse + dense pipeline (CPU-only, accuracy-first).
# NOTE: Dense (patch_match_stereo) may be VERY slow on CPU.
# If your COLMAP build refuses CPU dense, it will error at patch_match_stereo.

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def find_colmap() -> str:
    p = shutil.which("colmap")
    if p:
        return p
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
    if cmd and cmd[0] == "colmap":
        cmd = [COLMAP] + cmd[1:]
    print("\n>>>", " ".join(map(str, cmd)))
    subprocess.run(cmd, check=True)


def pick_model_folder(sparse_dir: Path) -> Path:
    if (sparse_dir / "0").is_dir():
        return sparse_dir / "0"
    candidates = [p for p in sparse_dir.iterdir() if p.is_dir() and p.name.isdigit()]
    if not candidates:
        raise FileNotFoundError(f"No sparse model folders found in: {sparse_dir}")
    return sorted(candidates, key=lambda p: int(p.name))[0]


def reconstruct_sparse_and_dense(
    images_dir: Path,
    out_dir: Path,
    matcher: str = "sequential",   # "sequential" | "exhaustive"
    single_camera: bool = True,

    # Accuracy-first knobs (you said we can ignore time for now)
    num_threads: int = 8,
    max_num_features: int = 8192,
    sequential_overlap: int = 12,

    # Dense quality knobs (accuracy-first, very slow on CPU)
    pm_num_samples: int = 15,              # higher = better/denser, slower
    pm_window_radius: int = 7,             # higher = smoother/robust, slower
    pm_num_iterations: int = 7,            # higher = refinement, slower
    geom_consistency: bool = True,
) -> None:
    images_dir = images_dir.resolve()
    out_dir = out_dir.resolve()
    if not images_dir.exists():
        raise FileNotFoundError(f"Images folder not found: {images_dir}")

    out_dir.mkdir(parents=True, exist_ok=True)
    db_path = out_dir / "database.db"
    sparse_dir = out_dir / "sparse"
    dense_dir = out_dir / "dense"
    sparse_dir.mkdir(exist_ok=True)
    dense_dir.mkdir(exist_ok=True)

    # Fresh start (recommended when changing settings)
    if db_path.exists():
        db_path.unlink()
    if sparse_dir.exists():
        # keep folder but COLMAP will write new model folders
        pass

    # 1) Feature extraction (CPU-only, accuracy-first)
    run([
        "colmap", "feature_extractor",
        "--database_path", str(db_path),
        "--image_path", str(images_dir),

        "--ImageReader.single_camera", "1" if single_camera else "0",

        "--FeatureExtraction.use_gpu", "0",
        "--FeatureExtraction.num_threads", str(num_threads),

        # More features = better coverage/robustness (slower)
        "--SiftExtraction.max_num_features", str(max_num_features),

        # If you kept images at ~1600–2000px, default max_image_size is fine.
        # If you use larger images, you can set e.g. 2400/3200 here:
        # "--SiftExtraction.max_image_size", "2400",
    ])

    # 2) Matching (CPU-only)
    if matcher == "sequential":
        run([
            "colmap", "sequential_matcher",
            "--database_path", str(db_path),
            "--SequentialMatching.overlap", str(sequential_overlap),

            "--FeatureMatching.use_gpu", "0",
            "--FeatureMatching.num_threads", str(num_threads),

            # Helpful for wide-baseline building shots, but can be slower:
            "--FeatureMatching.guided_matching", "1",
        ])
    elif matcher == "exhaustive":
        run([
            "colmap", "exhaustive_matcher",
            "--database_path", str(db_path),

            "--FeatureMatching.use_gpu", "0",
            "--FeatureMatching.num_threads", str(num_threads),
            "--FeatureMatching.guided_matching", "1",
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

    # 4) Undistort for dense
    run([
        "colmap", "image_undistorter",
        "--image_path", str(images_dir),
        "--input_path", str(model_dir),
        "--output_path", str(dense_dir),
        "--output_type", "COLMAP",
    ])

    # 5) Dense stereo (PatchMatch)
    # IMPORTANT: Some COLMAP builds require CUDA here. If it errors, your build
    # may not support CPU PatchMatch.
    run([
        "colmap", "patch_match_stereo",
        "--workspace_path", str(dense_dir),
        "--workspace_format", "COLMAP",
        "--PatchMatchStereo.geom_consistency", "true" if geom_consistency else "false",

        # Accuracy-first tuning:
        "--PatchMatchStereo.num_samples", str(pm_num_samples),
        "--PatchMatchStereo.window_radius", str(pm_window_radius),
        "--PatchMatchStereo.num_iterations", str(pm_num_iterations),
    ])

    # 6) Fuse depth maps into a dense point cloud
    fused_ply = dense_dir / "fused.ply"
    run([
        "colmap", "stereo_fusion",
        "--workspace_path", str(dense_dir),
        "--workspace_format", "COLMAP",
        "--input_type", "geometric" if geom_consistency else "photometric",
        "--output_path", str(fused_ply),
    ])
    print(f"\n✅ Dense point cloud saved to: {fused_ply}")

    # 7) Optional: Meshing (may not be included in every build)
    mesh_ply = dense_dir / "meshed-poisson.ply"
    try:
        run([
            "colmap", "poisson_mesher",
            "--input_path", str(fused_ply),
            "--output_path", str(mesh_ply),
        ])
        print(f"✅ Mesh saved to: {mesh_ply}")
    except subprocess.CalledProcessError:
        print("\n⚠️ poisson_mesher failed/not available in this build.")
        print("You can mesh fused.ply in MeshLab/CloudCompare/Blender instead.")


def main():
    parser = argparse.ArgumentParser(description="COLMAP sparse + dense pipeline (CPU-only, accuracy-first).")
    parser.add_argument("--images", type=str, required=True, help="Folder of (downscaled) images.")
    parser.add_argument("--out", type=str, default="recon", help="Output folder (default: recon).")
    parser.add_argument("--matcher", type=str, default="sequential", choices=["sequential", "exhaustive"])
    parser.add_argument("--multi_camera", action="store_true",
                        help="Set if images come from multiple cameras/lenses (disables single_camera).")

    # You said time/CPU usage can be ignored, but these still matter for feasibility.
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--max_features", type=int, default=8192)
    parser.add_argument("--overlap", type=int, default=12)

    # Dense tuning (accuracy-first)
    parser.add_argument("--pm_samples", type=int, default=15)
    parser.add_argument("--pm_radius", type=int, default=7)
    parser.add_argument("--pm_iters", type=int, default=7)
    parser.add_argument("--no_geom", action="store_true", help="Disable geometric consistency (usually worse).")

    args = parser.parse_args()

    reconstruct_sparse_and_dense(
        images_dir=Path(args.images),
        out_dir=Path(args.out),
        matcher=args.matcher,
        single_camera=not args.multi_camera,
        num_threads=args.threads,
        max_num_features=args.max_features,
        sequential_overlap=args.overlap,
        pm_num_samples=args.pm_samples,
        pm_window_radius=args.pm_radius,
        pm_num_iterations=args.pm_iters,
        geom_consistency=not args.no_geom,
    )


if __name__ == "__main__":
    main()
