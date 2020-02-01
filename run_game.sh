{
	make clean;
	rmmod mastermind;
	rm /dev/mastermind0;
	rm /dev/mastermind;
	make;
	insmod mastermind.ko mastermind_major=145; #mastermind_number=*, mastermind_max_guesses=*
	mknod /dev/mastermind0 c 145 0 -m 666;
	chmod 666 /dev/mastermind0;
	# To run the test uncomment below.
	# gcc -o test mastermind_test.c;
	# ./test;
}
