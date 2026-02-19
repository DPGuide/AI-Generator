import os
import sys
import time
import warnings
import subprocess
from PIL import Image
import torch
import cv2
import numpy as np
# WICHTIG: DPMSolver für schärfere Bilder
from diffusers import AnimateDiffPipeline, MotionAdapter, DPMSolverMultistepScheduler
from diffusers.utils import load_image

warnings.filterwarnings("ignore")

# --- DEINE PFADE ---
MOTION_ADAPTER_PATH = r"C:\AI\Models\motion_adapter"
SD15_PATH = r"C:\AI\Models\sd15"
RAW_OUTPUT = "final_ai_video.mp4"

# Standard-FPS (wird später dynamisch überschrieben)
FPS = 8 

# GTX 1060 Limit: 24 Frames passen in einen Chunk ohne Morphing
MAX_FRAMES_PER_CHUNK = 24 

def update_status(message):
    try:
        with open("ai_status.txt", "w", encoding="utf-8") as f:
            f.write(message)
        print(f"Status: {message}")
    except:
        pass

def generate_chunk(pipe, image, prompt, num_frames, chunk_num, total_chunks, generator):
    """Generiert einen Chunk mit fixiertem Seed"""
    
    base_progress = int((chunk_num / total_chunks) * 100)
    
    def progress_callback(step, timestep, latents):
        steps_total = 30 # num_inference_steps
        chunk_prog = int((step / steps_total) * (100 / total_chunks))
        overall = min(base_progress + chunk_prog, 100)
        if step % 5 == 0: 
            update_status(f"GEN: {overall}% (Part {chunk_num+1}/{total_chunks})")
            
    # Automatischer Qualitäts-Boost
    full_prompt = prompt + ", cinematic, high quality, 4k, highly detailed, sharp focus, epic lighting"

    result = pipe(
        prompt=full_prompt,
        negative_prompt="bad quality, blurry, low resolution, distorted, static, deformed, ugly, watermark, text, cartoon",
        image=image,
        num_inference_steps=30, # Hohe Qualität
        num_frames=num_frames,
        guidance_scale=12.0,    # Starker Fokus auf den Prompt
        callback=progress_callback,
        callback_steps=1,
        output_type="pil",
        save_memory=True,
        generator=generator
    )
    
    return result.frames[0]

def concatenate_videos(video_files, output_path):
    """Fügt Video-Schnipsel zusammen"""
    list_file = "concat_list.txt"
    with open(list_file, "w") as f:
        for vid in video_files:
            f.write(f"file '{vid}'\n")
    
    if os.path.exists(output_path):
        os.remove(output_path)

    cmd = [
        "ffmpeg", "-y", "-f", "concat", "-safe", "0",
        "-i", list_file,
        "-c", "copy",
        output_path
    ]
    subprocess.run(cmd, check=False, capture_output=True)
    
    if os.path.exists(list_file):
        os.remove(list_file)
    for vid in video_files:
        try:
            if os.path.exists(vid): os.remove(vid)
        except: pass

def generate_unlimited(image_path, prompt, total_frames_requested, seed):
    """Hauptlogik mit Dynamic Slow-Motion"""
    update_status("LOAD: MODELS...")
    
    adapter = MotionAdapter.from_pretrained(MOTION_ADAPTER_PATH, torch_dtype=torch.float16)
    pipe = AnimateDiffPipeline.from_pretrained(
        SD15_PATH,
        motion_adapter=adapter,
        torch_dtype=torch.float16
    )
    
    # SCHEDULER: DPM++ für Schärfe
    pipe.scheduler = DPMSolverMultistepScheduler.from_config(
        pipe.scheduler.config,
        algorithm_type="sde-dpmsolver++",
        use_karras_sigmas=True
    )
    
    pipe.enable_vae_slicing()
    pipe.enable_model_cpu_offload() 
    
    if seed == -1:
        seed = int(time.time())
    generator = torch.Generator("cuda").manual_seed(seed)
    print(f"Using Seed: {seed}")

    # --- HIER IST DIE MAGIE FÜR DEINE 1060 ---
    
    # 1. Wir erzwingen IMMER nur einen Chunk (kein Morphing)
    num_chunks = 1 
    actual_frames = MAX_FRAMES_PER_CHUNK # Das sind die 24 Frames
    
    # 2. Wir berechnen die gewünschte Zeit aus der C++ Eingabe
    # Annahme: C++ rechnet mit 24 FPS Basis. 
    # Wenn User "10" eingibt, sendet C++ 240 Frames.
    target_seconds = total_frames_requested / 24.0
    if target_seconds < 1: target_seconds = 1
    
    # 3. Wir berechnen die Abspielgeschwindigkeit (FPS) neu!
    # Wenn wir 24 Bilder haben und 10 Sekunden füllen müssen -> 2.4 FPS
    dynamic_fps = actual_frames / target_seconds
    
    # Sicherheitslimits für FPS
    if dynamic_fps < 1: dynamic_fps = 1
    if dynamic_fps > 30: dynamic_fps = 30
    
    update_status(f"SINGLE PASS: {actual_frames} frames @ {dynamic_fps:.2f} FPS (Target: {target_seconds:.1f}s)")
    
    image = load_image(image_path)
    image = image.resize((512, 512)) 
    
    chunk_files = []
    
    # Da num_chunks immer 1 ist, läuft das hier nur einmal durch
    for i in range(num_chunks):
        
        # Generieren
        chunk_frames = generate_chunk(pipe, image, prompt, actual_frames, i, num_chunks, generator)
        
        # Speichern mit DYNAMIC FPS
        chunk_file = f"temp_chunk_{i:03d}.mp4"
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        
        # HIER nutzen wir die berechnete Geschwindigkeit
        out = cv2.VideoWriter(chunk_file, fourcc, dynamic_fps, (512, 512))
        
        for frame in chunk_frames:
            cv_frame = cv2.cvtColor(np.array(frame), cv2.COLOR_RGB2BGR)
            out.write(cv_frame)
        out.release()
        
        chunk_files.append(chunk_file)
        torch.cuda.empty_cache() 
    
    update_status("FINALIZING...")
    concatenate_videos(chunk_files, RAW_OUTPUT)
    update_status("DONE")

def run_ai():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    
    if not os.path.exists("bastion_ai_task.txt"):
        return

    try:
        with open("bastion_ai_task.txt", "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f.readlines()]
            
        if len(lines) < 3: return

        img_path = lines[0]
        # C++ sendet Frames (z.B. 48 für 2s, 240 für 10s)
        total_frames_requested = int(lines[1]) 
        prompt = lines[2]
        
        seed = -1
        if len(lines) >= 4:
            if lines[3].isdigit():
                seed = int(lines[3])

        generate_unlimited(img_path, prompt, total_frames_requested, seed)

    except Exception as e:
        import traceback
        traceback.print_exc()
        update_status("FAILED")

if __name__ == "__main__":
    run_ai()