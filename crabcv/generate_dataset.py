import os, random
import cv2
import numpy as np


# CONSTANTS
# config
PROJECT = r"C:\Users\vzhang\OneDrive - Eastside Preparatory School\Documents\crabcv"
ASSETS  = os.path.join(PROJECT, "assets")

TARGET = os.path.join(ASSETS, "target.png") # labeled
OTHER1 = os.path.join(ASSETS, "other1.png") # distractor
OTHER2 = os.path.join(ASSETS, "other2.png") # distractor

OUT = os.path.join(PROJECT, "dataset")

IMG_SIZE = 640
N_TRAIN  = 800
N_VAL    = 200

# total crabs per image
MIN_TOTAL = 7
MAX_TOTAL = 15

# target count distribution
P_ZERO_TARGET = 0.15 # 15 perc imgs dont have target crab, reduce false positive
TARGET_MIN_NONZERO = 1
TARGET_MAX_NONZERO = 4

# scale ranges by species, some frac of imgsize
SCALE_TARGET = (0.12, 0.45)
SCALE_OTHER1 = (0.16, 0.45)
SCALE_OTHER2 = (0.18, 0.48)

ROT_RANGE_DEG = (-180, 180)

# placement constraints
MAX_TRIES_PER_OBJECT = 300
NO_OVERLAP_MARGIN_PX = 10

# tape style (around each crab)
TAPE_PROB_PER_CRAB = 0.98
TAPE_STRIPS_MIN = 1
TAPE_STRIPS_MAX = 2

# make background mostly white
BG_CLAMP_MIN = 240
BG_CLAMP_MAX = 255

SEED = 1337

# MAIN

random.seed(SEED)
np.random.seed(SEED)

# make sure directory exists
def ensure_dir(p):
    os.makedirs(p, exist_ok=True)

def load_rgba(path: str):
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise FileNotFoundError(f"Could not read image: {path}")
    if img.ndim != 3 or img.shape[2] not in (3, 4):
        raise ValueError(f"Expected 3 or 4 channels: {path} got {img.shape}")

    if img.shape[2] == 3:
        # best-effort alpha if no transparency
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        alpha = (gray < 245).astype(np.uint8) * 255
        img = np.dstack([img, alpha])
    return img

def ultra_white_background(size=640):
    # near white background w slight blue/green tint
    H = W = size
    base = random.randint(240, 255)

    # choose slight tint direction
    mode = random.random()
    if mode < 0.60:
        b, g, r = base, base, base  # pure-ish
    elif mode < 0.80:
        # tiny blue tint
        b = base
        g = base - random.randint(0, 1)
        r = base - random.randint(0, 2)
    else:
        # tiny green tint
        b = base - random.randint(0, 1)
        g = base
        r = base - random.randint(0, 2)

    bg = np.zeros((H, W, 3), dtype=np.uint8)
    bg[:, :] = (b, g, r)

    # micro gradient
    grad_strength = random.uniform(-0.8, 0.8)
    gradient = np.linspace(0, grad_strength, H).reshape(H, 1, 1)
    bg = np.clip(bg.astype(np.float32) + gradient, BG_CLAMP_MIN, BG_CLAMP_MAX).astype(np.uint8)

    # micro noise
    sigma = random.uniform(0.05, 0.25)
    noise = np.random.normal(0, sigma, bg.shape).astype(np.float32)
    bg = np.clip(bg.astype(np.float32) + noise, BG_CLAMP_MIN, BG_CLAMP_MAX).astype(np.uint8)

    return bg

def rotate_scale_rgba_no_chop(rgba: np.ndarray, angle_deg: float, out_w: int, out_h: int):
    h, w = rgba.shape[:2]
    pad = int(1.0 * max(h, w))  # padding for thin legs
    canvas = np.zeros((h + 2*pad, w + 2*pad, 4), dtype=np.uint8)
    canvas[pad:pad+h, pad:pad+w] = rgba

    ch, cw = canvas.shape[:2]
    center = (cw / 2, ch / 2)
    M = cv2.getRotationMatrix2D(center, angle_deg, 1.0)

    rotated = cv2.warpAffine(
        canvas, M, (cw, ch),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(0, 0, 0, 0),
    )

    alpha = rotated[:, :, 3]
    ys, xs = np.where(alpha > 10)
    if len(xs) == 0 or len(ys) == 0:
        return None

    x0, x1 = xs.min(), xs.max()
    y0, y1 = ys.min(), ys.max()
    cropped = rotated[y0:y1+1, x0:x1+1]

    if cropped.shape[0] < 5 or cropped.shape[1] < 5:
        return None

    resized = cv2.resize(cropped, (out_w, out_h), interpolation=cv2.INTER_AREA)
    return resized

# get bbox w/ transparency
def paste_rgba(bg_bgr: np.ndarray, fg_rgba: np.ndarray, x: int, y: int):
    H, W = bg_bgr.shape[:2]
    h, w = fg_rgba.shape[:2]
    if x < 0 or y < 0 or x + w > W or y + h > H:
        return None

    roi = bg_bgr[y:y+h, x:x+w]
    fg = fg_rgba

    alpha = (fg[:, :, 3:4].astype(np.float32) / 255.0)
    fg_rgb = fg[:, :, :3].astype(np.float32)
    roi_f = roi.astype(np.float32)

    blended = alpha * fg_rgb + (1 - alpha) * roi_f
    bg_bgr[y:y+h, x:x+w] = blended.astype(np.uint8)

    return (x, y, x + w, y + h)

# ensure crabs are spaced out w/ margin
def intersects(a, b, margin=10) -> bool:
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    return not (ax2 + margin <= bx1 or bx2 + margin <= ax1 or
                ay2 + margin <= by1 or by2 + margin <= ay1)

# convert bbox to yolo labels
def yolo_line(cls_id: int, box, img_w: int, img_h: int) -> str:
    x1, y1, x2, y2 = box
    bw = (x2 - x1) / img_w
    bh = (y2 - y1) / img_h
    cx = (x1 + x2) / 2 / img_w
    cy = (y1 + y2) / 2 / img_h
    return f"{cls_id} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}"

def add_tape_around_box(img_bgr: np.ndarray, box):
    """

    Docstring for add_tape_around_box
    
    :param img_bgr: Description
    :type img_bgr: np.ndarray
    :param box: Description
    """
    H, W = img_bgr.shape[:2]
    x1, y1, x2, y2 = box
    bw, bh = (x2 - x1), (y2 - y1)

    # Tape padding around the crab
    pad_x = int(random.uniform(0.06, 0.14) * bw)
    pad_y = int(random.uniform(0.06, 0.14) * bh)

    tx1 = max(0, x1 - pad_x)
    ty1 = max(0, y1 - pad_y)
    tx2 = min(W - 1, x2 + pad_x)
    ty2 = min(H - 1, y2 + pad_y)

    # Very light gray / near-white tape color
    base = random.randint(180, 255)
    tape_color = (base, base, base)

    # Vary opacity slightly
    alpha = random.uniform(0.05, 0.18)

    # Blend ONLY in the rectangle area
    roi = img_bgr[ty1:ty2, tx1:tx2]
    tape = np.full_like(roi, tape_color, dtype=np.uint8)

    blended = cv2.addWeighted(tape, alpha, roi, 1 - alpha, 0)
    img_bgr[ty1:ty2, tx1:tx2] = blended


def pick_scale_range(kind: str, did: int | None):
    if kind == "target":
        return SCALE_TARGET
    return SCALE_OTHER1 if did == 0 else SCALE_OTHER2

def make_split(split_name: str, n_images: int, target_rgba: np.ndarray, other_rgba_list: list[np.ndarray]):
    img_dir = os.path.join(OUT, "images", split_name)
    lbl_dir = os.path.join(OUT, "labels", split_name)
    ensure_dir(img_dir)
    ensure_dir(lbl_dir)

    for i in range(n_images):
        bg = ultra_white_background(IMG_SIZE) # 1. creates blank background
        labels = []
        placed_boxes = []

        total_crabs = random.randint(MIN_TOTAL, MAX_TOTAL)

        if random.random() < P_ZERO_TARGET:
            n_target = 0
        else:
            n_target = random.randint(TARGET_MIN_NONZERO, TARGET_MAX_NONZERO)
        n_target = min(n_target, total_crabs) # decides # objs
        n_other = total_crabs - n_target

        # keep distractors balanced
        objs = [("target", None)] * n_target
        distractor_ids = [0, 1] * ((n_other + 1) // 2)
        distractor_ids = distractor_ids[:n_other]
        random.shuffle(distractor_ids)
        for did in distractor_ids:
            objs.append(("other", did))
        random.shuffle(objs)

        for kind, did in objs:
            base = target_rgba if kind == "target" else other_rgba_list[did]
            scale_rng = pick_scale_range(kind, did)

            h0, w0 = base.shape[:2]
            target_dim = int(random.uniform(*scale_rng) * IMG_SIZE)
            target_dim = max(28, target_dim)

            scale = target_dim / max(h0, w0)
            out_w = max(14, int(w0 * scale))
            out_h = max(14, int(h0 * scale))

            angle = random.uniform(*ROT_RANGE_DEG)
            fg = rotate_scale_rgba_no_chop(base, angle, out_w, out_h)
            if fg is None:
                continue

            placed_ok = False
            for _try in range(MAX_TRIES_PER_OBJECT):
                x = random.randint(0, IMG_SIZE - out_w)
                y = random.randint(0, IMG_SIZE - out_h)
                bb = (x, y, x + out_w, y + out_h)

                if any(intersects(bb, pb, margin=NO_OVERLAP_MARGIN_PX) for pb in placed_boxes):
                    continue

                pasted = paste_rgba(bg, fg, x, y)
                if pasted is None:
                    continue

                placed_boxes.append(pasted)

                # light tape around each crab randomly
                if random.random() < TAPE_PROB_PER_CRAB:
                    add_tape_around_box(bg, pasted)#, max_strips=TAPE_STRIPS_MAX)

                # Label only target
                if kind == "target":
                    labels.append((0, pasted))

                placed_ok = True
                break

            # if crowded and can't place, skip this crab
            if not placed_ok:
                pass

        name = f"{split_name}_{i:05d}"
        cv2.imwrite(os.path.join(img_dir, name + ".jpg"), bg)

        with open(os.path.join(lbl_dir, name + ".txt"), "w", newline="\n") as f:
            for cls_id, box in labels:
                f.write(yolo_line(cls_id, box, IMG_SIZE, IMG_SIZE) + "\n")

        if (i + 1) % 100 == 0:
            print(f"{split_name}: {i+1}/{n_images}")

def main():
    ensure_dir(OUT)

    target_rgba = load_rgba(TARGET)
    other1_rgba = load_rgba(OTHER1)
    other2_rgba = load_rgba(OTHER2)

    make_split("train", N_TRAIN, target_rgba, [other1_rgba, other2_rgba])
    make_split("val",   N_VAL,   target_rgba, [other1_rgba, other2_rgba])

    yaml_path = os.path.join(PROJECT, "target_only.yaml")
    with open(yaml_path, "w", newline="\n") as f:
        f.write(
            "path: dataset\n"
            "train: images/train\n"
            "val: images/val\n"
            "names:\n"
            "  0: target_crab\n"
        )
    print("Wrote:", yaml_path)
    print("Dataset written to:", OUT)

if __name__ == "__main__":
    main()