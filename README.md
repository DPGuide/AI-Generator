Update is coming , be patient !

bash to pack both model´s folder , 
in 1 file , so 2 files at the end.
then you can throw them to a other place just as a backup.
#################################################################################

.\tba_packer.exe "C:\AI\Models\motion_adapter" "C:\AI\Models\motion_adapter.tba"

.\tba_packer.exe "C:\AI\Models\sd15" "C:\AI\Models\sd15.tba"


<img width="696" height="138" alt="image" src="https://github.com/user-attachments/assets/70d7c63f-fc3c-48b7-8e53-b0dba39e588e" />

#################################################################################

<img width="400" height="229" alt="image" src="https://github.com/user-attachments/assets/05f315fc-a64a-44ab-a419-ade5ff02b1b5" />
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
