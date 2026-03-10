import os
import cv2
import numpy as np


def extract_frames_evenly(n: int,
                          video_path: str | None = None,
                          data_dir: str | None = None,
                          images_dir: str | None = None) -> list[str]:

    if n < 1:
        raise ValueError("`n` must be >= 1")

    script_dir = os.path.dirname(os.path.abspath(__file__))
    if data_dir is None:
        data_dir = os.path.join(script_dir, "data")
    if images_dir is None:
        images_dir = os.path.join(script_dir, "images")

    if not os.path.isfile(video_path):
        raise FileNotFoundError(f"video file does not exist: {video_path}")

    os.makedirs(images_dir, exist_ok=True)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"failed to open video: {video_path}")

    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if total_frames <= 0:
        cap.release()
        raise RuntimeError("video contains no frames")

    # choose evenly spaced frame indices
    indices = np.linspace(0, total_frames - 1, n, dtype=int)

    out_files: list[str] = []
    pad = len(str(n))
    for i, idx in enumerate(indices, start=1):
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(idx))
        success, frame = cap.read()
        if not success or frame is None:
            # this can happen if the index is beyond the last frame
            continue
        out_name = f"frame_{i:0{pad}d}.jpg"
        out_path = os.path.join(images_dir, out_name)
        cv2.imwrite(out_path, frame)
        out_files.append(out_path)

    cap.release()
    return out_files

# or specify everything
frames = extract_frames_evenly(
    100,
    video_path=r"data\coral_garden.MOV",
    images_dir=r"images",
)