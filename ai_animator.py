# ai_animator_unlimited.py - Für lange Videos!
import os
import sys
import time
import warnings
import subprocess
from PIL import Image
import torch
from diffusers import AnimateDiffPipeline, MotionAdapter, EulerDiscreteScheduler
from diffusers.utils import export_to_video, load_image
import cv2
import numpy as np

warnings.filterwarnings("ignore")

MOTION_ADAPTER_PATH = r"C:\AI\Models\motion_adapter"
SD15_PATH = r"C:\AI\Models\sd15"
TARGET_WIDTH = 640
TARGET_HEIGHT = 480
FPS = 24
MAX_FRAMES_PER_CHUNK = 12  # Für GTX 1060 6GB sicher

def update_status(message):
    with open("ai_status.txt", "w", encoding="utf-8") as f:
        f.write(message)
    print(f"Status: {message}")

def generate_chunk(pipe, image, prompt, num_frames, start_step=0, total_steps=25):
    """Generiert einen Chunk von Frames"""
    
    def progress_callback(step, timestep, latents):
        overall_step = start_step + step
        percent = int((overall_step / total_steps) * 100)
        update_status(f"GENERATING: {percent}%")
    
    result = pipe(
        prompt=prompt,
        image=image,
        num_frames=num_frames,
        num_inference_steps=22,
        guidance_scale=7.5,
        callback=progress_callback,
        callback_steps=1,
        generator=torch.Generator("cuda").manual_seed(int(time.time()))
    )
    
    return result.frames[0]

def generate_long_video(image_path, prompt, total_frames):
    """Generiert langes Video in Chunks"""
    
    update_status("LOADING: 10%")
    
    # Pipeline laden (einmal!)
    adapter = MotionAdapter.from_pretrained(
        MOTION_ADAPTER_PATH, 
        torch_dtype=torch.float16, 
        local_files_only=True
    )
    
    pipe = AnimateDiffPipeline.from_pretrained(
        SD15_PATH, 
        motion_adapter=adapter, 
        torch_dtype=torch.float16, 
        local_files_only=True
    )
    
    pipe.scheduler = EulerDiscreteScheduler.from_config(
        pipe.scheduler.config, 
        beta_schedule="linear"
    )
    pipe.enable_vae_slicing()
    pipe.enable_attention_slicing(1)
    pipe.to("cuda")
    
    update_status("LOADING: 50%")
    
    image = load_image(image_path)
    
    # Berechne Chunks
    num_chunks = (total_frames + MAX_FRAMES_PER_CHUNK - 1) // MAX_FRAMES_PER_CHUNK
    all_frames = []
    
    update_status(f"CHUNKS: {num_chunks} parts")
    time.sleep(1)
    
    for i in range(num_chunks):
        frames_in_chunk = min(MAX_FRAMES_PER_CHUNK, total_frames - len(all_frames))
        chunk_start = i * MAX_FRAMES_PER_CHUNK
        
        update_status(f"CHUNK {i+1}/{num_chunks}: {frames_in_chunk} frames")
        
        # Letzter Frame vom vorherigen Chunk als neuer Start (für Kontinuität)
        if i > 0 and len(all_frames) > 0:
            # Nimm letzten Frame als Image für nächsten Chunk
            last_frame = all_frames[-1]
            image = last_frame
        
        chunk_frames = generate_chunk(
            pipe, image, prompt, frames_in_chunk,
            start_step=i * 25,  # Progress tracking
            total_steps=num_chunks * 25
        )
        
        # Überschneidung vermeiden (letzten Frame nicht doppelt)
        if i > 0:
            all_frames.extend(chunk_frames[1:])
        else:
            all_frames.extend(chunk_frames)
        
        # Speicher freigeben
        torch.cuda.empty_cache()
    
    return all_frames

def upscale_frames(frames, width, height):
    """Upscale alle Frames zu Full HD"""
    update_status("UPSCALING: 90%")
    
    upscaled = []
    for i, frame in enumerate(frames):
        # OpenCV für schnelles Upscale
        frame_array = np.array(frame)
        upscaled_frame = cv2.resize(frame_array, (width, height), interpolation=cv2.INTER_LANCZOS4)
        upscaled.append(Image.fromarray(upscaled_frame))
        
        if i % 10 == 0:
            percent = 90 + int((i / len(frames)) * 6)
            update_status(f"UPSCALING: {percent}%")
    
    return upscaled

def save_video(frames, output_path, fps):
    """Speichert Frames als MP4"""
    update_status("SAVING: 98%")
    
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    out = cv2.VideoWriter(output_path, fourcc, fps, (frames[0].width, frames[0].height))
    
    for frame in frames:
        # PIL zu OpenCV (RGB zu BGR)
        cv_frame = cv2.cvtColor(np.array(frame), cv2.COLOR_RGB2BGR)
        out.write(cv_frame)
    
    out.release()

def run_ai():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    
    if not os.path.exists("bastion_ai_task.txt"):
        update_status("ERROR: NO_TASK")
        return

    with open("bastion_ai_task.txt", "r", encoding="utf-8") as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]

    if len(lines) < 3:
        update_status("ERROR: BAD_FORMAT")
        return

    img_path = lines[0]
    # Jetzt: SEKUNDEN statt Frames!
    seconds = int(lines[1]) if lines[1].isdigit() else 2
    prompt = lines[2]
    
    # Sekunden zu Frames
    total_frames = seconds * FPS
    # Max 10 Sekunden pro Durchlauf (sonst zu lang)
    total_frames = min(total_frames, 240)  # Max 10 Sekunden = 240 Frames
    
    update_status(f"START: {seconds}s = {total_frames} frames")

    try:
        # Bild vorbereiten
        img = Image.open(img_path).convert("RGB").resize((512, 512))
        temp_img = "temp_512.jpg"
        img.save(temp_img, "JPEG", quality=95)
        
        # Langes Video generieren
        frames = generate_long_video(temp_img, prompt, total_frames)
        
        # Upscale zu 1080p
        hd_frames = upscale_frames(frames, TARGET_WIDTH, TARGET_HEIGHT)
        
        # Speichern
        save_video(hd_frames, "aion_o.mp4", FPS)
        
        # Cleanup
        if os.path.exists(temp_img):
            os.remove(temp_img)
        
        update_status("DONE")
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        update_status("FAILED")

if __name__ == "__main__":
    run_ai()