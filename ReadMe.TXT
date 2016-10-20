DualMode Library release 6
==========================

Written by Marty Amodeo (mamodeo@stny.rr.com)


For documentation on how to use this library, please read through the included
file Dualmode.h.  It contains the developer's interface as well as all of the
documentation you should need to get started writing dual mode applications.


What's new with release 6
-------------------------

* Mouse absolute positioning in PM mode changed
  While in PM mode when the mouse is grabbed, mouse movement is restricted
  to the size of the playfield without polling and recentering the mouse.
  This should make for less processor "noise" and smoother operation and
  mouse readings.
  
* Completely customizable and programmable keyboard interface
  Keys on the keyboard can now be programmed to perform a time-dependent
  sequence of events, like rapid fire or those tricky little "dragon punch"
  Street Fighter moves.  ;-)
  
* Standard GUI interface for keyboard settings
  Your application can call a function to pop up a dialog window which can
  maintain your programmable keyboard settings and these settings can be
  saved in your application's INI file.

* Direct framebuffer blitters cleaned up a bit
  The direct framebuffer blitters will now clip properly to the size of the
  window and will clip around windows that are placed on top of your window.
  They're still a bit sloppy, but generally seem to get the job done.
  
* Blitter support for complex shapes
  Support was added for placing a complex shaped window on top of your
  DualMode application.  So anyone with Sit-O-CHIRARI or similar enhancer
  running won't have to worry about it interfering anymore.

* Bug fixed on potentially improper full screen shutdown procedures

* Added preliminary support for BGR3/4 source format images
  This has not been implemented for full screen sessions yet.

* Example application updated
  DMLibTest now will accept keyboard and mouse input to demonstrate how to
  use more of the features of the DualMode library.  It will draw a colorful
  bitmap and allow you to switch full screen by pressing any key other than
  ESC (which will exit) or 1 (which will bring up the keyboard settings).
  While the app is running, you will be able to draw in white on the bitmap
  using the mouse.


Cautions and notes
------------------

* The internal DIVE blitter will not be able to draw your window properly
  if a complex window shape is placed on top of your window, requiring more
  than 39 rectangles to describe the visible area.  This is a DIVE limitation
  and does not apply to any of the custom blitters in the DualMode.  If you
  want it fixed, tell IBM.
  
* Be aware that when you customize the keyboard, when you tell it to press a
  key, you must also tell it to release the key otherwise the application
  might think that the key is stuck down.
  
* If you are utilizing the dialog resources compiled into the DualMode DLL
  itself (using the MGLSC_customizeKeyboard API call), you must make sure
  that the DLL itself is named "DUALMODE.DLL".  Otherwise the dialog resources
  will not be found.
  
* Direct framebuffer blitters will sometimes distort their images slightly
  (by one or two pixel rows) when a window is placed on top of them.  I'll
  work on cleaning this up soon.

* To allow non-SDD users to run applications made with MGLServer, the users
  must boot up with the included (or a more recent) version of
  drivers\SDDHelp.Sys in the MGLServer archive.  You should also have a
  "drivers" directory under your program directory containing SDDDaemn.Exe
  and Graphics.Bpd as I have done in the MGLServer archive.


Distribution
------------

This library can be freely distributed with or without source code.  It may be
used in and distributed with freeware, shareware, or commercial applications,
so long as credit is given to the original author.  This library may not be
sold as a stand-alone package nor can it be merely wrapped by another library
and sold without the presence of a commercial application utilizing it.  This
source code can be modified as needed, though as a courtesy to me (the
original author), I'd appreciate hearing about any improvements that you've
made so that I can make the library better for everyone.  If you have any
question about fair usage of this library, feel free to contact me directly.


Please send any comments, bug reports, and suggestions to mamodeo@stny.rr.com.

