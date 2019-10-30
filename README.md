# pitimer

A very sparse sort of logic analyzer for measuring long duration events using
a Raspberry Pi's GPIO pins.

## TODO
* Remove hardcoded pin numbers and names
* Add CLI options to specify pin numbers and names
* Write pin changes to CSV file
* Add real-time output with time and time since last change (CLI option)
* Allow delay between samples to be specified
* Quiet option when writing to CSV
* Terminate on specific pin changing state (maybe?)
* Run for fixed length of time or specify end time
