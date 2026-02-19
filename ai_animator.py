import os
import sys
import time
import warnings
import subprocess
import shutil
import tempfile
import struct
import io
from PIL import Image
import torch
import cv2
import numpy as np

# Diffusers Importe
from diffusers import AnimateDiffPipeline, MotionAdapter, DPMSolverMultistepScheduler
from diffusers.utils import load_image

warnings.filterwarnings("ignore")

# =================================================================
# --- BASTION TBA LOADER (V5 SMART & LOUD VERSION) ---
# =================================================================
class TBALoader:
    def __init__(self, archive_path):
        self.archive_path = archive_path
        self.files = {}
        self._index_archive()

    def _index_archive(self):
        if not os.path.exists(self.archive_path):
            raise FileNotFoundError(f"Archive {self.archive_path} not found")
        with open(self.archive_path, "rb") as f:
            if f.read(4) != b"TBA1":
                raise ValueError("Invalid TBA Format")
            
            count_data = f.read(4)
            file_count = struct.unpack('<I', count_data)[0]
            ENTRY_SIZE = 96 # Jetzt 96 Bytes wegen 64-Bit!
            
            for _ in range(file_count):
                entry_data = f.read(ENTRY_SIZE)
                if len(entry_data) < ENTRY_SIZE: break
                
                # Q steht in Python für 64-Bit unsigned long long
                name_bytes, offset, orig_size, comp_size, ftype, padding = struct.unpack('<64sQQQII', entry_data)
                filename = name_bytes.decode('utf-8').rstrip('\x00').replace('\\', '/')
                self.files[filename] = {"offset": offset, "size": comp_size}

    def extract_to_temp(self, target_folder):
        print(f"\n[TBA] Mounting {os.path.basename(self.archive_path)}...")
        extracted_count = 0
        with open(self.archive_path, "rb") as f:
            for filename, info in self.files.items():
                # Filtert die Gigabyte-Fresser raus
                if any(x in filename for x in ["fp32", ".bin", ".ckpt", "non_ema"]):
                    continue
                
                out_path = os.path.join(target_folder, filename)
                os.makedirs(os.path.dirname(out_path), exist_ok=True)
                
                f.seek(info["offset"])
                with open(out_path, "wb") as f_out:
                    # SPEED-UP: 4 MB Chunks anstatt 64 KB! Das gibt Python den Turbo.
                    bytes_left = info["size"]
                    chunk_size = 4 * 1024 * 1024 
                    while bytes_left > 0:
                        chunk = f.read(min(chunk_size, bytes_left))
                        f_out.write(chunk)
                        bytes_left -= len(chunk)
                
                print(f" -> Entpackt: {filename}")
                extracted_count += 1
                
        print(f"[TBA] ERFOLG: {extracted_count} Dateien in den RAM geladen!\n")
        return target_folder

# =================================================================
# --- PFADE & KONFIGURATION ---
# =================================================================
# Die TBA-Dateien müssen so heißen wie der Ordner + .tba
MOTION_ADAPTER_PATH = r"C:\AI\Models\motion_adapter"
SD15_PATH = r"C:\AI\Models\sd15"
RAW_OUTPUT = "final_ai_video.mp4"

MAX_FRAMES_PER_CHUNK = 24 

def update_status(message):
    try:
        with open("ai_status.txt", "w", encoding="utf-8") as f:
            f.write(message)
        print(f"Status: {message}")
    except: pass

def mount_tba_smart(tba_name, original_path):
    tba_file = original_path + ".tba"
    if os.path.exists(tba_file):
        try:
            temp_dir = tempfile.mkdtemp(prefix=f"bastion_{tba_name}_")
            loader = TBALoader(tba_file)
            loader.extract_to_temp(temp_dir)
            update_status(f"TBA MOUNTED: {tba_name}")
            return temp_dir, temp_dir
        except Exception as e:
            print(f"TBA ERROR: {e}")
    
    update_status(f"FALLBACK: Using folder for {tba_name}")
    return original_path, None

def generate_chunk(pipe, image, prompt, num_frames, generator):
    def progress_callback(step, timestep, latents):
        if step % 5 == 0: update_status(f"GEN: {int(step/30*100)}%")
            
    full_prompt = prompt + ", cinematic, high quality, 4k, highly detailed, sharp focus, epic lighting"
    result = pipe(
        prompt=full_prompt,
        negative_prompt="bad quality, blurry, low resolution, distorted, static, deformed, ugly, watermark, text",
        image=image,
        num_inference_steps=30,
        num_frames=num_frames,
        guidance_scale=12.0,
        callback=progress_callback,
        callback_steps=1,
        output_type="pil",
        generator=generator
    )
    return result.frames[0]

def run_ai():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    if not os.path.exists("bastion_ai_task.txt"): 
        print("Kein Task gefunden.")
        return

    temp_dirs = []
    try:
        with open("bastion_ai_task.txt", "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f.readlines()]
        if len(lines) < 3: return

        img_path = lines[0]
        total_frames_requested = int(lines[1]) 
        prompt = lines[2]
        seed = int(lines[3]) if len(lines) >= 4 and lines[3].isdigit() else int(time.time())

        update_status("SYSTEM INIT...")

        # 1. MOUNTEN DER ARCHIVE
        sd_working_path, sd_cleanup = mount_tba_smart("SD15", SD15_PATH)
        if sd_cleanup: temp_dirs.append(sd_cleanup)
            
        motion_working_path, motion_cleanup = mount_tba_smart("MOTION", MOTION_ADAPTER_PATH)
        if motion_cleanup: temp_dirs.append(motion_cleanup)

        # 2. PIPELINE LADEN
        update_status("LOADING MODELS...")
        adapter = MotionAdapter.from_pretrained(
            motion_working_path, 
            torch_dtype=torch.float16,
            use_safetensors=True
        )
        
        pipe = AnimateDiffPipeline.from_pretrained(
            sd_working_path, 
            motion_adapter=adapter, 
            torch_dtype=torch.float16,
            variant="fp16",       # WICHTIG: Sagt der KI, dass die dicken Dateien fehlen dürfen!
            use_safetensors=True  # Zwingt die KI, die schnellen Safetensors zu nutzen
        )
        
        # Scheduler für Top-Qualität wieder einbinden!
        pipe.scheduler = DPMSolverMultistepScheduler.from_config(
            pipe.scheduler.config, 
            algorithm_type="sde-dpmsolver++", 
            use_karras_sigmas=True
        )
        
        # Speicheroptimierung für GTX 1060
        pipe.enable_vae_slicing()
        pipe.enable_model_cpu_offload() 
        
        generator = torch.Generator("cuda").manual_seed(seed)
        
        # FPS Logik
        actual_frames = MAX_FRAMES_PER_CHUNK
        dynamic_fps = 8.0 # Standard
        
        update_status("GENERATING...")
        image = load_image(img_path).resize((512, 512))
        chunk_frames = generate_chunk(pipe, image, prompt, actual_frames, generator)
        
        # 3. VIDEO SPEICHERN
        chunk_file = "temp_output.mp4"
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        out = cv2.VideoWriter(chunk_file, fourcc, dynamic_fps, (512, 512))
        for frame in chunk_frames:
            out.write(cv2.cvtColor(np.array(frame), cv2.COLOR_RGB2BGR))
        out.release()
        
        if os.path.exists(RAW_OUTPUT): os.remove(RAW_OUTPUT)
        os.rename(chunk_file, RAW_OUTPUT)
        
        update_status("DONE")

    except Exception as e:
        import traceback
        traceback.print_exc()
        update_status(f"FAILED: {str(e)}")
    
    finally:
        # 4. RADIKALER CLEANUP
        for d in temp_dirs:
            if os.path.exists(d):
                shutil.rmtree(d)
                print(f"[CLEANUP] Removed {d}")

if __name__ == "__main__":
    run_ai()