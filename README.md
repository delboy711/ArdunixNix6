# ArdunixNix6
Nixie Clock based on Arduino<br>

This is a fork of Ian Smarts excellent Nixie Clock<br>
https://github.com/isparkes/ArdunixNix6<br>

When building my own clock I made a few modifications to the code to add some extra features.<br>
* Supports 0.96" I2C OLED display using SSD1306Wire library.
* Displays weather reports from UK Met Office for the local vicinity.
* Uses NTP for time updates instead of non standard server. (Corrected for timezone and DST).
* Includes facility for microwave movement detector to blank tubes when room is unoccupied. (Uses RCWL-0516).
<br>
For the NTP updates I used a Wemos D1 mini ESP8266 board, but any ESP8266 based device would work OK.
Because I was only doing this for my own clock some features such as using UK Met Office are hard coded.<br>
There is a new option defined in the clock configuration to allow disabling the movement detector blanking.<br>

![Nixie clock](/images/dereks_nixie.jpg)

#####################################################################################<br>

<br>
//**********************************************************************************<br>
//**********************************************************************************<br>
//* Main code for an Arduino based Nixie clock. Features:                          *<br>
//*  - Real Time Clock interface for DS3231                                        *<br>
//*  - Digit fading with configurable fade length                                  *<br>
//*  - Digit scrollback with configurable scroll speed                             *<br>
//*  - Configuration stored in EEPROM                                              *<br>
//*  - Low hardware component count (as much as possible done in code)             *<br>
//*  - Single button operation with software debounce                              *<br>
//*  - Single 74141 for digit display (other versions use 2 or even 6!)            *<br>
//*  - Highly modular code                                                         *<br>
//*  - RGB Back lighting                                                           *<br>
//*  - Automatic dimming using light sensor                                        *<br>
//*                                                                                *<br>
//*  isparkes@protonmail.ch                                                        *<br>
//*                                                                                *<br>
//**********************************************************************************<br>
//**********************************************************************************<br>
<br>
ardunixFade9_6_digit.ino: Main code for the 6 Digit Nixie Clock<br>
<br>
<strong>Instruction and User Manuals (including schematic) can be found at:</strong>
<br>
    http://www.open-rate.com/Manuals.html<br>
<br>
You can buy this from:
<br>
    http://www.open-rate.com/Store.html<br><br>
<br>
<br>
<strong>Construction and prototyping:</strong><br>
hvTest.ino: code for testing the HV generation<br>
buttonTest.ino: Code for testing button presses<br>
<br>
<br>
YouTube video of version 42 of the clock in action:<br>
<br>
https://youtu.be/9lNWKlWbXSg<br>
<br>
YouTube video of an early version of the clock in action:<br>
<br>
    https://www.youtube.com/watch?v=Js-7MJpCtvI<br>
<br>
