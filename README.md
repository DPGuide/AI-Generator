***Update is coming , be patient ! under construction***
***optimized for GTX1060 you can change by better grafic cards the ai_animated.py
"VRAM lifesaver approach" (16 frames, 448x448 resolution and xformers)***

***PURGE CACHE implemented***
#################################################################################
***All files which you realy need on C:,but make a backup of the models,
on a other partition D: or where ever.
. The SD15 Folder
Go to C:\AI\Models\sd15. Delete everything there except for these folders
(which you copied from the deep .cache maze to the very beginning):
feature_extractor,scheduler,text_encoder,tokenizer,unet,vae.
model_index.json (This file is the key; it must be located here!)
2. The Motion Adapter Folder
Go to C:\AI\Models\motion_adapter. Only these two files should be located here:
config.json
diffusion_pytorch_model.safetensors***

now use the packer
.\tba_packer.exe "C:\AI\Models\motion_adapter" "C:\AI\Models\motion_adapter.tba"
.\tba_packer.exe "C:\AI\Models\sd15" "C:\AI\Models\sd15.tba"


<img width="863" height="112" alt="image" src="https://github.com/user-attachments/assets/b94da981-4bdd-4b4f-b3b7-f351b8758676" />

#################################################################################

<img width="403" height="256" alt="image" src="https://github.com/user-attachments/assets/0a80b013-ad2f-489f-b80b-f004ac57c34e" />

Bastion AI Nexus - Tool new function:
What does this "seed" do?
In the AI ​​world (as with Stable Diffusion or AnimateDiff), every image or video is initially created from pure,
random "image noise." The seed determines what this noise looks like.

Fixed value (e.g., 42): If you leave the value at 42, load the same image, and enter the same text,
the AI ​​will produce the exact same video every time. This is extremely important
if you're refining a prompt and want to see what changes without randomness interfering.

The "R" button: When you click the R (Random), your C++ program generates a completely new,
random number in the field. When you then click Generate,
the AI ​​calculates a completely different movement for your image, even though the text remains the same!


Bonus: Find Faster & Better Models

Once this path bug is fixed, the system will run like a dream. Here are the promised goldmines for better models:

Civitai (The best place to start): Search for "SD 1.5 Checkpoints". Popular classics include Realistic Vision and Epic Realism.

Hugging Face (The technical source):

Realistic Vision V6.0 B1 – Enormous level of detail for humans.

DreamShaper 8 – Very versatile, great colors.

***links / url will be added soon***
