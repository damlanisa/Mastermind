# Mastermind
Character device driver module on Linux to play the Mastermind game in terminal with bash commands, written in C. System Programming course project.
Project is explained at p1.pdf file.

module_parameters:

	mastermind_number
	
	mastermind_max_guesses
	
	mastermind_major
	
	
To install the module and create a device please run the command below:

	sudo bash run_game.sh
	
	
If you want to test ioctl commands please uncomment the lines in the
bash script. If you encounter any error you can check logs for further
information.


You can see the logs created by the device in runtime with the command below:

	dmesg
