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

-------------------------------------------------------------------------------
1.) What's translated in this patch?
-------------------------------------------------------------------------------
Story dialog
Battle dialog
Minigames
Battle menus
System menus
Added subtitles to movies(!)
Load Screens
Bootup screen

-------------------------------------------------------------------------------
2.) Release History
-------------------------------------------------------------------------------
December 12, 2019 V 1.00

-------------------------------------------------------------------------------
3.) Patching Instructions
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
		disc in IsoBuster. Right-click on the top-level CD icon and do as follows:
		Extract CD <Content> -> Extract User Data (*.tao, *.iso, *.img, *.wav)

	b) 	Save the image in a directory of your choice. 

	c) 	When the copy reaches 99%, you'll receive an "Unreadable Sector" prompt. 
		Select "Replace with all zeros," check the "Always apply Selection" box, and click the
		"Selection" button. The image copy will complete.

	d) You should end up with two files:
		Track 01.iso
		Track 02.wav. 

3) Apply the Patch
	a) Back up your iso in case the patch fails.
	
	b) Using DeltaPatcher.exe (*NOT DeltaPatcherLite.exe*), apply the appropriate patch to your Track 01.iso file.
	
	c) If the patch was successful, you'll recieve a prompt saying "Patch successfully applied".

	d) Do this for both discs.
	
4) Place the provided cue files into the same directory as each of your isos.  One for Disc1, one for Disc2
	a) If your iso is called anything besides "Track 01.iso", modify the cue files using Notepad.exe so that the iso name matches.
 	
5) Run the game!
	a) Use mednafen to run the game by providing it the cue file.

~Optional~
6) Burn a CD-R for your Saturn.
	Pre-Reqs:
		a) Your Saturn will need to be modded in order to play cd-r discs and you will need an Action Replay to play a Japanese Region game. 
		b) Or you can use PseudoSaturn on an unmodded Saturn.
	
	a) Use your favorite burning software to burn a cd-r.  (I have used UltraISO for my own tests.)
	b) Use the CUE files to burn each disc.
	
-------------------------------------------------------------------------------
4.) Compatibility & Troubleshooting
-------------------------------------------------------------------------------

*This patch has been tested using Mednafen 1.22.2 and also on a modded Sega Saturn + Action Replay.

It has been reported by one user that the patch does not run on Rhea setups.

-------------------------------------------------------------------------------
5.) Translation Credits	
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
MatatabiMitsu

==TRANSLATION LEADS==
MatatabiMitsu
Schlomo
CrouchingMouse
Nezumi

==TRANSLATION TEAM==
Kate Robinson 
Eien Ni Hen
Cal Hampton
Eben Williams
Margie Obendorf
Sarah Dopierala
Mason van Dyk

==GRAPHICS MODIFICATION==
Joqu
Ibnu
NoahSteam
TrekkiesUnite118

==TESTERS==
BowlOfLentils
cj_iwakura
CrouchingMouse
PsionicStorm
Paragonias

==TALENT RECRUITMENT==
cj_iwakura
Kevin Walters (Turbo857)

==TRAILER==
Edited by TrekkiesUnite118 and Bowl of Lentils
VO by CrouchingMouse
Narration Script by Eien1No1Yami and CJ Iwakura

-------------------------------------------------------------------------------
6.) Acknowledgements & Special Thanks
-------------------------------------------------------------------------------
EsperKnight    - For some great hacking advice.  The script would have been butchered in certain parts of the game were it not for his suggestion of how to cram so much text into the given amount of space.
CybwerWarriorX - For providing some very initial clues into how the text in SakuraWars is stored.
ItsumoKnight   - For managing the discord channel and giving us all some awesome icons :)
Nym            - For introducing cj_iwakura to the Russian PC version of the game.
Victor Ireland - For inspiring fan translators and having tried to bring Sakura Wars to the West.
Sega and RED   - For creating this amazing game and a badass console!
FaustWolf      - For introducing me to the amazing world of fan translations, Saturn hacking, 
				 showing me that what seems impossible can be made possible, 
				 and even providing a template for this readme :)

(The folling is shamelessly taken from Dragon Force II's readme because it's all still true!)

*To Guillaume Duhamel, Theo Berkau, Anders Montonen, and EsperKnight for making Yabause's
 debugger the most user-friendly tracing tool out there, and for making the
 Sega Saturn one of the most well-documented consoles. 

If you're interested in the technical side of Sega Saturn translation projects, 
 make wiki.yabause.org your one stop info shop!

*To everyone involved with Mednafen, by far the best Saturn emulator I have used.

*To SadNESCity and PhOeNiX, for making Delta Patcher.

*To the Romhacking.net community, for being awesome, and for keeping a central 
 hub for fannish works alive. 
