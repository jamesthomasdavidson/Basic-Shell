James Davidson - V00812527

References
	During this assignment I used GNU's c library manual as a reference.  Particularily Section 28.6,
	(http://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html#Implementing-a-Shell
	I used it to model my initialize_shell() function and execute_job().

About the Project
	I used an array of pointers to store all of my background jobs in the form of a struct.  Every struct contained all of the necessary information for fork() and execvp() calls.
	Jobs are assigned a job number which is always one more than the current highest job number currently active (regardless of whether the job is running or stopped).  This 
	allows jobs to have a job number of say 7 when in fact the maximum amount of jobs allowed is 5.  All of the constants (such as MAX_JOBS) are defined at the top of the main.c file
	allowing for further flexibility.

	Hope you enjoy! 
	Happy marking :)
