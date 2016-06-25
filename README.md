Morphological operators for GIMP
================================
A set of morphological operators for GIMP, created by Alessandro Francesconi. 
Homepage with some examples: http://www.alessandrofrancesconi.it/projects/morphop


This plugin extends the GIMP's default filters "Erode" and "Dilate" with more features:

 * More operators:
	* Opening and Closing
	* Boundary Extraction
	* Gradient
	* Hit-or-Miss
	* Thickening
	* Thinning
	* Skelethonization
	* White and Black Top Hat

 * Possibility to change the structuring element's shape and size


Compiling and installing under Linux/Unix
-----------------------------------------

You must install libgimp2.0-dev package in order to have 
the full set of libraries and dependences for compiling this plugin.
Then:

	make && make install
	
Or:

	make && sudo make install-admin

To make and install for every user in the system (needs root privileges).


Installing under Windows
-------------------------

This plugin is already compiled for Windows and it's saved in bin/win32 directory.
Just copy morphop.exe into the default GIMP-plugins folder:
`<Programs-dir>\<GIMP-folder>\lib\gimp\<version>\plug-ins`
or
`<User-dir>\.gimp<version>\plug-ins`


Support this project
--------------------

Visit http://github.com/alessandrofrancesconi/gimp-plugin-morphop/issues
for posting bugs and enhancements. Make it better!
