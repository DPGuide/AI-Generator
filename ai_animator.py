import os, sys, time, warnings, shutil, tempfile, struct, torch, cv2
import numpy as np
from PIL import Image
from diffusers import AnimateDiffPipeline, MotionAdapter, DPMSolverMultistepScheduler
from diffusers.utils import load_image

warnings.filterwarnings("ignore")

# =================================================================
# --- BASTION TBA LOADER (V7 - 64 BIT) ---
# =================================================================
class TBALoader:
    def __init__(self, archive_path):
        self.archive_path = archive_path
        self.files = {}
        self._index_archive()

    def _index_archive(self):
        with open(self.archive_path, "rb") as f:
            if f.read(4) != b"TBA1": raise ValueError("Kein TBA Format")
            file_count = struct.unpack('<I', f.read(4))[0]
            for _ in range(file_count):
                entry = f.read(96) 
                name, off, o_sz, c_sz, ftype, pad = struct.unpack('<64sQQQII', entry)
                fname = name.decode('utf-8').rstrip('\x00').replace('\\', '/')
                self.files[fname] = {"offset": off, "size": c_sz}

    def extract_to_temp(self, target_folder):
        print(f"[TBA] Entpacke {os.path.basename(self.archive_path)}...")
        with open(self.archive_path, "rb") as f:
            for fname, info in self.files.items():
                if any(x in fname for x in ["fp32", ".bin", ".ckpt", "non_ema"]): continue
                out = os.path.join(target_folder, fname)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                f.seek(info["offset"])
                with open(out, "wb") as f_out:
                    f_out.write(f.read(info["size"]))
        return target_folder

def update_status(msg):
    try:
        with open("ai_status.txt", "w", encoding="utf-8") as f: f.write(msg)
        print(f"Status: {msg}")
    except: pass

# =================================================================
# --- MARATHON GENERIERUNG (CHUNK-BY-CHUNK) ---
# =================================================================
def run_ai():
    if not os.path.exists("bastion_ai_task.txt"): return

    with open("bastion_ai_task.txt", "r", encoding="utf-8") as f:
        lines = [l.strip() for l in f.readlines()]
    
    img_path = lines[0]
    total_frames = int(lines[1]) if lines[1].isdigit() else 16
    prompt = lines[2]
    seed = int(lines[3]) if len(lines) > 3 and lines[3].isdigit() else int(time.time())

    temp_dirs = []
    try:
        # 1. Archive Mounten
        sd_base = r"C:\AI\Models\sd15"
        mo_base = r"C:\AI\Models\motion_adapter"
        
        update_status("MOUNTING ARCHIVES...")
        sd_tmp = TBALoader(sd_base + ".tba").extract_to_temp(tempfile.mkdtemp(prefix="b_sd_"))
        mo_tmp = TBALoader(mo_base + ".tba").extract_to_temp(tempfile.mkdtemp(prefix="b_mo_"))
        temp_dirs.extend([sd_tmp, mo_tmp])

        # 2. Pipeline laden mit EXTREM-VRAM-SPARMODUS
        update_status("LOADING AI (ULTRA SAVE)...")
        adapter = MotionAdapter.from_pretrained(mo_tmp, torch_dtype=torch.float16)
        pipe = AnimateDiffPipeline.from_pretrained(sd_tmp, motion_adapter=adapter, torch_dtype=torch.float16, variant="fp16")
        
        pipe.scheduler = DPMSolverMultistepScheduler.from_config(pipe.scheduler.config, algorithm_type="sde-dpmsolver++", use_karras_sigmas=True)
        
        # Die Retter der 1060:
        pipe.enable_vae_slicing()
        pipe.enable_vae_tiling() # WICHTIG: Erlaubt 512x512 ohne VRAM-Spitzen
        pipe.enable_model_cpu_offload() # Parkt Modelle im RAM statt VRAM
        
        # 3. Marathon-Schleife (Chunking)
        all_frames = []
        current_image = load_image(img_path).resize((512, 512))
        generator = torch.Generator("cuda").manual_seed(seed)
        
        # Wir rendern immer 16 Frames pro Stück
        CHUNK_SIZE = 16 
        total_chunks = (total_frames + CHUNK_SIZE - 1) // CHUNK_SIZE
        
        update_status(f"STARTING MARATHON ({total_frames} Frames)...")

        for i in range(total_chunks):
            chunk_num = i + 1
            update_status(f"CHUNK {chunk_num}/{total_chunks}...")
            
            # Berechne wie viele Frames in diesem Chunk (für den Rest am Ende)
            frames_to_gen = min(CHUNK_SIZE, total_frames - len(all_frames))
            
            output = pipe(
                prompt=prompt + ", high quality, cinematic",
                negative_prompt="bad quality, blurry, flicker",
                image=current_image,
                num_frames=frames_to_gen,
                num_inference_steps=25,
                guidance_scale=8.0,
                generator=generator,
                output_type="pil"
            )
            
            chunk_frames = output.frames[0]
            all_frames.extend(chunk_frames)
            
            # Das letzte Bild dieses Chunks wird das Startbild für den nächsten
            current_image = chunk_frames[-1]
            
            # Status-Update für die GUI
            percent = int((len(all_frames) / total_frames) * 100)
            update_status(f"GEN: {percent}%")

        # 4. Finales Video speichern
        update_status("SAVING LONG VIDEO...")
        out_file = "final_ai_video.mp4"
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        vw = cv2.VideoWriter(out_file, fourcc, 8.0, (512, 512))
        
        for frame in all_frames:
            vw.write(cv2.cvtColor(np.array(frame), cv2.COLOR_RGB2BGR))
        vw.release()
        
        update_status("DONE")

    except Exception as e:
        import traceback
        traceback.print_exc()
        update_status(f"FAILED: {str(e)}")
        input("Crash! Drücke ENTER...")

    finally:
        for d in temp_dirs:
            if os.path.exists(d): shutil.rmtree(d)

if __name__ == "__main__":
    run_ai()