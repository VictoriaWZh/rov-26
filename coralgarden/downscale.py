from pathlib import Path
import cv2

INPUT = Path("images")
OUTPUT = Path("images_downscaled")
OUTPUT.mkdir(exist_ok=True)

MAX_SIZE = 1600  # max width/height

for img_path in INPUT.glob("*"):
    if img_path.suffix.lower() not in [".jpg", ".jpeg", ".png"]:
        continue

    img = cv2.imread(str(img_path))
    h, w = img.shape[:2]

    scale = MAX_SIZE / max(h, w)
    if scale < 1:
        new_w = int(w * scale)
        new_h = int(h * scale)
        img = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)

    out_path = OUTPUT / img_path.name
    cv2.imwrite(str(out_path), img)

print("Done downscaling!")
