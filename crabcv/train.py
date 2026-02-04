# train_oneclass.py
from ultralytics import YOLO

path = r"C:\Users\vzhang\OneDrive - Eastside Preparatory School\Documents\crabcv\crab_one.yaml"

model = YOLO("yolov8n.pt")
model.train(
    data=path,
    epochs=45,
    imgsz=640,
    batch=16,
    lr0=0.01,
    patience=10,
)
