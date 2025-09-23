#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Function to check if a character is a separator
int is_separator(char c) {
    // Separators: '-', '\r', '\n', '.', '/'
    char *separators = "-\r\n./";
    for(int i = 0; separators[i]; i++) {
        if(c == separators[i]) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Check if filename is provided
    if(argc != 2) {
        fprintf(2, "Usage: sixfive <filename>\n");
        exit(1);
    }
    
    // Open the file
    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        fprintf(2, "sixfive: cannot open %s\n", argv[1]);
        exit(1);
    }
    
    char buffer[1];  // Read one character at a time
    char number[32]; // Buffer to store current number
    int num_index = 0;
    int in_number = 0;
    
    // Read file character by character
    while(read(fd, buffer, 1) > 0) {
        char c = buffer[0];
        
        // If current character is a digit, add to current number
        if(c >= '0' && c <= '9') {
            number[num_index++] = c;
            in_number = 1;
        } 
        // If character is separator and we were building a number, process it
        else if(is_separator(c) && in_number) {
            number[num_index] = '\0'; // Null-terminate the number string
            
            // Convert string to integer
            int num = atoi(number);
            
            // Check if multiple of 5 or 6
            if(num % 5 == 0 || num % 6 == 0) {
                printf("%d\n", num);
            }
            
            // Reset for next number
            num_index = 0;
            in_number = 0;
        }
        // If character is not digit and not separator, reset number building
        else {
            num_index = 0;
            in_number = 0;
        }
    }
    
    // Process last number if file ends with a number
    if(in_number) {
        number[num_index] = '\0';
        int num = atoi(number);
        if(num % 5 == 0 || num % 6 == 0) {
            printf("%d\n", num);
        }
    }
    
    close(fd);
    exit(0);
}
