## Slab6_Flatpak_Linux
<img width="1600" height="900" alt="imagen" src="https://github.com/user-attachments/assets/fe7c3e9c-1131-41b5-88b4-a08b90c3c464" />


## What is this?
Unofficial port and repackage of Slab6 as a Flatpak replacing the assembly files and windows dependencies into something that works for Linux.
This approach was chosen because I'm working on making more Linux-friendly some tools I use for modding or editing files.

## Why not upload this directly to Flathub?

This Flatpak doesn't comply with the submit requirements of Flathub.  
Specially this one.  

So if you, ***the reader*** can do this again and make it comply with Flathub please do, I don't know how many people apart from me would benefit to have Slab6 being more accesible on Linux but if it does for you I'm happy it helps someone.


## Why not pushing this onto the official repository?
There isn't one.
Slab6 was developed decades ago for Windows specifically.
There's no much point on keep on developing the tool which is understandable but this is for the people now going to a new Linux based OS.

Yes, you can also run Slab6 trough Proton, but it's buggy running it trough Wine/Proton so a native approach works better.



# Installation:
Download the "Slab6" Flatpak file to a directory with user privileges, your "Downloads" folder should do.  
Install the flatpak by running from terminal:  
- flatpak install --user Slab6.flatpak  
  
You can open Slab6 by running:  
- flatpak run advsys.net.Slab6  

Or you can just log off and log in from your session.  
The Slab6 program should at this point be added to your menu ^^

# Uninstall:
If you ever need to uninstall this Flatpak just run from terminal:
-  flatpak uninstall --user advsys.net.Slab6

