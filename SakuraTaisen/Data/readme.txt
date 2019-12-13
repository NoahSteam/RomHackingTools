-------------------------------------------------------------------------------
Sakura Wars Tranlslation Project
Sakura Wars
December 12, 2019
http://sakurawarstranslation.com/
-------------------------------------------------------------------------------
CONTENTS
-------------------------------------------------------------------------------
1.) What's translated in this patch?
2.) Release History
3.) Patching Instructions
4.) Compatibility Troubleshooting
5.) Translation Credits
6.) Acknowledgements & Special Thanks

What's translated in this patch?
Story dialog
Battle dialog
Minigames
Battle menus
System menus
Added subtitles to movies(!)
Load Screens
Bootup screen

-------------------------------------------------------------------------------
1.) Release History
-------------------------------------------------------------------------------
December 12, 2019 V 1.00

-------------------------------------------------------------------------------
2.) Patching Instructions
-------------------------------------------------------------------------------

==REQUIRED TOOLS==

*IsoBuster: Free download @ http://www.isobuster.com/download.php
 (For ripping an image from the Japanese game CD)

*SadNESCity's Delta Patcher. Included in the patch zip file. The most recent one can always be found @ http://www.romhacking.net/utils/704/
 (For applying the translation patch -- this one's required!)

*Mednafen Emulator": 
 Free download @ https://mednafen.github.io/releases/

==INSTRUCTIONS==

*NOTE: If you have already backed up Sakura Wars to a .ISO or a .BIN/.CUE
 image, please re-rip following these instructions, opening the image in ISOBuster
 as if it were your physical game CD.

1) Unzip the translation patches for Sakura Wars archive into directories of your choice.

2) Create the iso Image:
	a)	Pop your Sakura Wars CD into your computer's CD/DVD ROM drive. Open the 
		disc in IsoBuster. RigIht-click on the top-level CD icon and do as follows:
		Extract CD <Content> -> Extract User Data (*.tao, *.iso, *.img, *.wav)

	b) 	Save the image in a directory of your choice. 

	c) 	When the copy reaches 99%, you'll receive an "Unreadable Sector" prompt. 
		Select "Replace with all zeros," check the "Always apply Selection" box, and click the
		"Selection" button. The image copy will complete.

	d) You will end up with a file called Track 01.iso and Track 02.wav (depending on your cdrom drive there might be more wav files). 

3) Apply the Patch
	a) Back up your iso in case the patch fails.
	
	b) Using DeltaPatcher.exe (*NOT DeltaPatcherLite.exe*), apply the appropriate patch to your Track 01.iso file.
	
	c) If the patch was successful, you'll recieve a prompt saying "Patch successfully applied"

4) Place the provided cue files into the same directory as each of your isos.  One for Disc1, one for Disc2
	a) If your iso is called anything besides "Track 01.iso", modify the cue files using Notepad.exe so that the iso name matches.
 	
5) Run the game!
	a) Use mednafen to run the game.

~Optional~
6) Burn a CD-R for your Saturn.
	Pre-Reqs:
		a) Your Saturn will need to be modded in order to play cd-r discs and you will need an Action Replay to play a Japanese Region game. 
		b) Or you can use PseudoSaturn on an unmodded Saturn.
	
	a) Use your favorite burning software to burn a cd-r.  (I have used UltraISO for my own tests.)
	b) Use the CUE files to burn each disc.
	
-------------------------------------------------------------------------------
3.) Compatibility & Troubleshooting
-------------------------------------------------------------------------------

*This patch has been tested using Mednafen 1.22.2 and also on a modded Sega Saturn + Action Replay

-------------------------------------------------------------------------------
4.) Translation Credits	
-------------------------------------------------------------------------------

==PROJECT LEAD==
NoahSteam

==HACKERS==
NoahSteam
TrekkiesUnite118

==LEAD EDITOR==
cj_iwakura

==EDITORS==
BowlOfLentils
CrouchingMouse
Schlomo

==TRANSLATION LEADS==
Getsuya
Schlomo
CrouchingMouse
Nezumi

==TRANSLATION TEAM==
Kate Robinson 
Eien Ni Hen
Cal Hampton

==GRAPHICS MODIFICATION==
Joqu
Ibnu
NoahSteam

==TALENT RECRUITMENT==
cj_iwakura
Kevin Walters (Turbo857)

==TRAILER==
Edited by TrekkiesUnite118 and Bowl of Lentils
VO by CrouchingMouse
Narration Script by Eien1No1Yami and CJ Iwakura

-------------------------------------------------------------------------------
5.) Acknowledgements & Special Thanks
-------------------------------------------------------------------------------
EsperKnight    - For some great hacking advice
CybwerWarriorX - For providing some very initial clues into how the text in SakuraWars is stored
ItsumoKnight   - For managing the discord channel and giving us all some awesome icons :)
Nym
Victor Ireland - For inspiring fan translators
Sega and RED   - For creating this amazing game and a badass console!

*The header font used on the monarch select bios is based on the fonts from 
 "The Legend of Zelda: Windwaker" (captured by AleX_XelA) and "Tales of 
  Symphonia: Dawn of the New World" (captured by Mageker). The font for the 
  domestics screen buttons is based on the font from "Actraiser" (captured by
  Stangmar).

*To Guillaume Duhamel, Theo Berkau, and Anders Montonen, for making Yabause's
 debugger the most user-friendly tracing tool out there, and for making the
 Sega Saturn one of the most well-documented consoles. 

 If you're interested in the technical side of Sega Saturn translation projects, 
 make wiki.yabause.org your one stop info shop!

*To Barubary, author of TiledGGD. This helped immensely throughout the 
 Dragon Force II translation project! There's nothing like being able to pop in
 an entire memory dump and find out how a game writes its screen coordinates to 
 a tee. FW owes you a beer sometime!

*To SnowBro, author of TileMolester. Still the user-friendly standard for 
 editing and re-inserting game graphics.

*To SadNESCity and PhOeNiX, for making Delta Patcher.

*To the Romhacking.net community, for being awesome, and for keeping a central 
 hub for fannish works alive. 

*To ZeaLitY and the entire Kajar Labs crew. Not only did you take FW from PoliSci
 student to budding game modder while showing him the Springtime of Youth, you 
 created a really cool readme format that FW totally ripped off for purposes of
 this entire document! 

*To Working Designs and the Sega Saturn and Dragon Force fanbase, for giving the 
 translation team something to live up to and a reason to do something totally,
 totally ambitious and cool.
