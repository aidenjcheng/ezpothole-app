import gradio as gr
import cv2
import numpy as np
from ultralytics import YOLO
from fastapi import FastAPI, UploadFile, File, Form, Request
from fastapi.responses import JSONResponse
import uvicorn
from collections import defaultdict
import time
import io
from PIL import Image
import os
from supabase import create_client, Client
import random
from google import genai
from google.genai import types
import re

model = YOLO("best.pt")


SUPABASE_URL = os.environ.get("SUPABASE_URL")
SUPABASE_KEY = os.environ.get("SUPABASE_KEY")
supabase: Client = create_client(SUPABASE_URL, SUPABASE_KEY) if SUPABASE_URL and SUPABASE_KEY else None


gps_cache = defaultdict(list)
MAX_GPS_CACHE_SIZE = 100

POTHOLE_THRESHOLD = 0

FONT = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE = 1
TEXT_POS = (40, 80)
FONT_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 255)

def process_image(image, imgsz=640, conf=0.25):
    """Process image through YOLO model and return annotated frame + damage percentage"""
    if image is None:
        return None, "No image provided."
    
    frame = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)
    
    results = model.predict(source=frame, imgsz=imgsz, conf=conf, verbose=False)
    processed_frame = results[0].plot(boxes=False)
    
    percentage_damage = 0.0
    if results[0].masks is not None:
        total_area = 0.0
        masks = results[0].masks.data.cpu().numpy()
        image_area = frame.shape[0] * frame.shape[1]
        
        for mask in masks:
            binary_mask = (mask > 0).astype(np.uint8) * 255
            contour, _ = cv2.findContours(binary_mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
            if contour:
                total_area += cv2.contourArea(contour[0])
        
        percentage_damage = (total_area / image_area) * 100.0
        
        cv2.line(
            processed_frame,
            (TEXT_POS[0], TEXT_POS[1] - 10),
            (TEXT_POS[0] + 350, TEXT_POS[1] - 10),
            BG_COLOR,
            40,
        )
        cv2.putText(
            processed_frame,
            f"Road Damage: {percentage_damage:.2f}%",
            TEXT_POS,
            FONT,
            FONT_SCALE,
            FONT_COLOR,
            2,
            cv2.LINE_AA,
        )
        
        processed_frame = cv2.cvtColor(processed_frame, cv2.COLOR_BGR2RGB)
        return processed_frame, f"Road Damage: {percentage_damage:.2f}%"
    else:
        processed_frame = cv2.cvtColor(processed_frame, cv2.COLOR_BGR2RGB)
        return processed_frame, "No damage detected."

def find_nearest_gps(session_id, img_timestamp, max_time_diff=2):
    """find the nearest GPS coordinate within max_time_diff seconds"""
    if session_id not in gps_cache or not gps_cache[session_id]:
        return None

    closest = min(gps_cache[session_id], key=lambda x: abs(x[0] - img_timestamp))
                
    if abs(closest[0] - img_timestamp) <= max_time_diff:
        return closest[1], closest[2]
    
    return None

def upload_to_supabase(image_bytes, lat, lon, session_id, timestamp):
    """uplod pothole detection to Supabase storage and database"""
    if not supabase:
        print("Warning: Supabase not configured")
        return False
    
    try:
        filename = f"{session_id}_{timestamp}_{random.randint(1000, 9999)}.jpg"
        
        storage_response = supabase.storage.from_("potholes").upload(
            filename,
            image_bytes,
            {"content-type": "image/jpeg"}
        )
        
        image_url = supabase.storage.from_("potholes").get_public_url(filename)
        
        pothole_name = f"Detected Pothole - #{random.randint(10000, 99999)}"
        db_response = supabase.table("potholes").insert({
            "name": pothole_name,
            "latitude": lat,
            "longitude": lon,
            "created_by": None
        }).execute()
        
        print(f"Successfully uploaded pothole: {pothole_name} at ({lat}, {lon})")
        return True
        
    except Exception as e:
        print(f"Error uploading to Supabase: {e}")
        return False
        
def process_and_annotate_image(image_bytes, imgsz=640, conf=0.1):
    """Process image, annotate it, and return annotated image bytes"""

    image_array = np.frombuffer(image_bytes, dtype=np.uint8)
    frame = cv2.imdecode(image_array, cv2.IMREAD_COLOR)
    
    if frame is None:
        return None, 0.0
    
    results = model.predict(source=frame, imgsz=imgsz, conf=conf, verbose=False)
    processed_frame = results[0].plot(boxes=False)
    
    percentage_damage = 0.0
    
    if results[0].masks is not None:
        total_area = 0.0
        masks = results[0].masks.data.cpu().numpy()
        image_area = frame.shape[0] * frame.shape[1]
        
        for mask in masks:
            binary_mask = (mask > 0).astype(np.uint8) * 255
            contour, _ = cv2.findContours(binary_mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
            if contour:
                total_area += cv2.contourArea(contour[0])
        
        percentage_damage = (total_area / image_area) * 100.0
        
        cv2.line(
            processed_frame,
            (TEXT_POS[0], TEXT_POS[1] - 10),
            (TEXT_POS[0] + 350, TEXT_POS[1] - 10),
            BG_COLOR,
            40,
        )
        cv2.putText(
            processed_frame,
            f"Road Damage: {percentage_damage:.2f}%",
            TEXT_POS,
            FONT,
            FONT_SCALE,
            FONT_COLOR,
            2,
            cv2.LINE_AA,
        )
    
    success, buffer = cv2.imencode('.jpg', processed_frame)
    if not success:
        return None, percentage_damage
    
    return buffer.tobytes(), percentage_damage

def process_image_with_detection(image_bytes, session_id, img_timestamp, imgsz=640, conf=0.1):
    """Process image, detect potholes, and upload to Supabase if detected"""
    image_array = np.frombuffer(image_bytes, dtype=np.uint8)
    frame = cv2.imdecode(image_array, cv2.IMREAD_COLOR)
    
    if frame is None:
        return {"error": "Could not decode image"}, 0.0
    
    results = model.predict(source=frame, imgsz=imgsz, conf=conf, verbose=False)
    
    percentage_damage = 0.0
    pothole_detected = False
    
    if results[0].masks is not None:
        total_area = 0.0
        masks = results[0].masks.data.cpu().numpy()
        image_area = frame.shape[0] * frame.shape[1]
        
        for mask in masks:
            binary_mask = (mask > 0).astype(np.uint8) * 255
            contour, _ = cv2.findContours(binary_mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
            if contour:
                total_area += cv2.contourArea(contour[0])
        
        percentage_damage = (total_area / image_area) * 100.0
        
        if percentage_damage > POTHOLE_THRESHOLD:
            pothole_detected = True
            
            gps_coords = find_nearest_gps(session_id, img_timestamp)
            
            if gps_coords:
                lat, lon = gps_coords
                upload_to_supabase(image_bytes, lat, lon, session_id, img_timestamp)
            else:
                print(f"No GPS data found for session {session_id} near timestamp {img_timestamp}")
    
    return {
        "pothole_detected": pothole_detected,
        "damage_percentage": round(percentage_damage, 2),
        "session_id": session_id
    }, percentage_damage


app = FastAPI()

@app.post("/upload")
async def upload_endpoint(
    request: Request,
    session_id: str = Form(None),
    type: str = Form(None),
    timestamp: int = Form(None),
    image: UploadFile = File(None),
    lat: float = Form(None),
    lon: float = Form(None)
):
    """
    Unified endpoint for both GPS and image uploads
    
    GPS upload: session_id, type="gps", timestamp, lat, lon
    Image upload: session_id, type="image", timestamp, image file
    """
    
    if not type and not image:
        try:
            body = await request.json()
            session_id = body.get("session_id")
            type = body.get("type")
            timestamp = body.get("timestamp")
            lat = body.get("lat")
            lon = body.get("lon")
        except:
            return JSONResponse({"error": "Invalid request format"}, status_code=400)
    
    if not session_id or not type or not timestamp:
        return JSONResponse({"error": "Missing required fields"}, status_code=400)
    
    if type == "gps":
        if lat is None or lon is None:
            return JSONResponse({"error": "Missing GPS coordinates"}, status_code=400)
        
        gps_cache[session_id].append((timestamp, lat, lon))
        
        if len(gps_cache[session_id]) > MAX_GPS_CACHE_SIZE:
            gps_cache[session_id] = gps_cache[session_id][-MAX_GPS_CACHE_SIZE:]
        
        return JSONResponse({
            "status": "success",
            "message": "GPS data received",
            "session_id": session_id,
            "cache_size": len(gps_cache[session_id])
        })
    
    elif type == "image":
        if not image:
            return JSONResponse({"error": "No image file provided"}, status_code=400)
        
        image_bytes = await image.read()
        
        result, damage_pct = process_image_with_detection(
            image_bytes, session_id, timestamp, imgsz=640, conf=0.1
        )
        
        return JSONResponse({
            "status": "success",
            "message": "Image processed",
            **result
        })
    
    else:
        return JSONResponse({"error": "Invalid type. Must be 'gps' or 'image'"}, status_code=400)


@app.post("/test")
async def test_endpoint(image: UploadFile = File(...)):
    """
    Test endpoint: Upload image, annotate it with YOLO, and store in Supabase
    No GPS coordinates required - just stores the annotated image on sum diddy blud ban ban
    """
    if not supabase:
        return JSONResponse(
            {"error": "Supabase not configured"}, 
            status_code=500
        )
    
    try:

        image_bytes = await image.read()
        
        annotated_bytes, damage_pct = process_and_annotate_image(
            image_bytes, imgsz=640, conf=0.1
        )
        
        if annotated_bytes is None:
            return JSONResponse(
                {"error": "Failed to process image"}, 
                status_code=400
            )
        
        timestamp = int(time.time())
        filename = f"test_{timestamp}_{random.randint(1000, 9999)}.jpg"
        
        storage_response = supabase.storage.from_("potholes").upload(
            filename,
            annotated_bytes,
            {"content-type": "image/jpeg"}
        )
        
        image_url = supabase.storage.from_("potholes").get_public_url(filename)
        
        return JSONResponse({
            "status": "success",
            "message": "Annotated image uploaded to Supabase",
            "filename": filename,
            "image_url": image_url,
            "damage_percentage": round(damage_pct, 2)
        })
        
    except Exception as e:
        return JSONResponse(
            {"error": f"Upload failed: {str(e)}"}, 
            status_code=500
        )

@app.get("/")
async def root():
    return {"message": "Pothole Detection API is running"}

@app.get("/health")
async def health():
    return {
        "status": "healthy",
        "model_loaded": model is not None,
        "supabase_configured": supabase is not None,
        "active_sessions": list(gps_cache.keys()),
        "total_gps_points": sum(len(v) for v in gps_cache.values())
    }

def build_ui():
    # don't think this works lol
    with gr.Blocks(title="Pothole Damage Estimator") as demo:
        gr.Markdown("# Pothole Damage Estimator")
        gr.Markdown("Upload an image to detect road damage. The system also accepts data from ESP32 camera and iPhone GPS via API.")
        
        with gr.Row():
            image = gr.Image(type="numpy", label="Upload an image")
        with gr.Row():
            imgsz = gr.Slider(320, 1280, value=640, step=64, label="Inference size (imgsz)")
            conf = gr.Slider(0.05, 0.9, value=0.25, step=0.05, label="Confidence")
        run_btn = gr.Button("Run Detection")
        out_img = gr.Image(type="numpy", label="Annotated detection frame")
        out_text = gr.Textbox(label="Damage %", interactive=False)
        
        run_btn.click(process_image, [image, imgsz, conf], [out_img, out_text])
    
    return demo

gradio_app = build_ui()
app = gr.mount_gradio_app(app, gradio_app, path="/")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=7860)