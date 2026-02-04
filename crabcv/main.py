# detect_oneclass.py
import cv2
from ultralytics import YOLO

WEIGHTS = r'C:\Users\vzhang\OneDrive - Eastside Preparatory School\Documents\crabcv\runs\detect\train\weights\best.pt'
INPUT_IMAGE = "input.png"
OUTPUT_IMAGE = "output.png"

def main():
    model = YOLO(WEIGHTS)
    results = model.predict(source=INPUT_IMAGE, conf=0.35, iou=0.55, verbose=False)[0]

    img = cv2.imread(INPUT_IMAGE)
    if img is None:
        raise FileNotFoundError(INPUT_IMAGE)

    count = 0
    if results.boxes is not None:
        for b in results.boxes:
            x1, y1, x2, y2 = map(int, b.xyxy[0].tolist())
            conf = float(b.conf.item())
            count += 1
            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
            cv2.putText(img, f"{conf:.2f}", (x1, max(0, y1-6)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    cv2.putText(img, f"COUNT: {count}", (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 0, 0), 4)
    cv2.putText(img, f"COUNT: {count}", (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)

    cv2.imwrite(OUTPUT_IMAGE, img)
    print("Count =", count)
    print("Saved:", OUTPUT_IMAGE)

if __name__ == "__main__":
    main()
