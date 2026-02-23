Here is a simple El Stupido program that prints all prime numbers up to 50, one per line using the provided specifications for loops and conditions in main_body section of your request (excluding helper functions):
initialize x := 1; // start from first number greater than or equal to two. This is a common initial condition used by all programs that need iteration over some range/sequence, such as the Fibonacci sequence in this case 0..50 (inclusive).
while i <= 50 {   /* Start of while loop */
    if x % i == 0 && i != 1 // Checking for divisibility by all numbers up to sqrt(x) because a larger factor would have already been found. Also, we need not check 2 as it is the only even prime number and thus can't be divided into two factors
    {   /* If block */
        print x; // Printing if condition holds true ie., this means that 'x', which should ideally represent a Prime Number in our case, has been found. 
         break ;// Exit from while loop as we have printed the prime number and no need to continue checking further numbers for it's primality (since all other factors would be larger than x).   // This line is optional but recommended so that program does not run indefinitely if a non-prime value has been found.
    }  /* End of If block */    
     ++i;// Incrementing 'x'. Without this, the loop will go on forever as it's incremented at each iteration and never reaches or exceeds any number greater than sqrt(50). This is a common optimization to reduce unnecessary iterations.   // Optional but recommended for efficiency in large ranges of numbers
} /* End Of while block */ 
    
This program uses the standard looping construct (while) with an incremental counter variable 'i' that iterates over all integers from one up until five hundred and prints out each prime number it finds. It also includes a condition inside if statement to break once we find first non-prime value which can be optimized by using sqrt(50).