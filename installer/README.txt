AMALTHEA INSTALLER - TRIMUI Brick (TG3040)
==========================================

Amalthea replaces the Brick's operating system with Amalthea Linux, a minimal
custom distribution that runs only the Amalthea instruments. It is not an app on
top of the stock firmware. Read https://nzimas.github.io/amalthea/install.html
first. You do this at your own risk; there is no warranty of any kind.

You need: the Brick, charged to at least 50%; a USB-C DATA cable; a computer
(macOS, Linux or Windows). ADB is offered for download if it is not installed.

INSTALL OR UPDATE
  macOS / Linux:  open a terminal in this folder and run   ./install.sh
  Windows:        double-click install.bat

  On a stock Brick the installer first copies the stock system to this folder
  (backup-<serial>). KEEP THAT FOLDER: it is how you go back to stock.
  On a Brick already running Amalthea, the same command updates it and first
  copies your saved sessions to this folder (sessions-<serial>-<date>).

GO BACK TO THE STOCK SYSTEM
  macOS / Linux:  ./restore.sh            (or ./restore.sh path/to/backup-folder)
  Windows:        double-click restore.bat (or restore.bat -Backup path\to\folder)

Nothing on the SD card, the stock settings partition or the internal data
partition is erased. Only the boot partition (p4) and root partition (p5) are
written, each checked by SHA-256 after writing.

Source, manual and changelog: https://github.com/nzimas/amalthea
