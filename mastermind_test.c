#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include "mastermind_ioctl.h"

int main()
{
    int pFile;
    char read_buffer[4096];
    char write_buffer[5];
    int res;
    
    pFile = open("/dev/mastermind0", O_RDWR);

    strcpy(write_buffer, "1111\n");
    write(pFile, write_buffer, sizeof(char) * 5);
    
    strcpy(write_buffer, "2222\n");
    write(pFile, write_buffer, sizeof(char) * 5);
    
    strcpy(write_buffer, "3333\n");
    write(pFile, write_buffer, sizeof(char) * 5);
	
	res = read(pFile, read_buffer, 15);
	read_buffer[res] = '\0';
	printf("%s", read_buffer);
	
	res = read(pFile, read_buffer, 3);
	read_buffer[res] = '\0';
	printf("%s", read_buffer);
	
	res = read(pFile, read_buffer, 5);
	read_buffer[res] = '\0';
	printf("%s", read_buffer);
    
    res = ioctl(pFile, MASTERMIND_REMAINING);
    printf("\nRemaining guesses: %d\n", res);
    
    ioctl(pFile, MASTERMIND_ENDGAME);
    printf("Game ended. Guess history and count are refreshed.\n");
    
	strcpy(write_buffer, "3333\n");
    write(pFile, write_buffer, sizeof(char) * 5);

	res = read(pFile, read_buffer, 64);
	read_buffer[res] = '\0';
	printf("%s", read_buffer);

    ioctl(pFile, MASTERMIND_NEWGAME, write_buffer);
    printf("New game has been started. Mastermind number has been changed.\n");
    
	strcpy(write_buffer, "3333\n");
    write(pFile, write_buffer, sizeof(char) * 5);

	res = read(pFile, read_buffer, 64);
	read_buffer[res] = '\0';
	printf("%s", read_buffer);
    
	close(pFile);
    return 0;
}
