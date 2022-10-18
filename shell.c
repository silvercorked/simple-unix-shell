/*
 * Author: Alex Wissing
 * Date: 7-30-2022  (Due 7-31-2022)
 * Course: CSCI-4500 (Sec: 850)
 * Pre-proj#: 5 (batch-line parser)
 * Desc: Batchline parser file, which reads and tokenizes simple commands in a shell-esk style.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> // strtok, strlen
#include <sys/wait.h> // wait

const unsigned int MAX_PATH_VAR_LENGTH = 256;
const unsigned int MAX_PATH_LENGTH = 256;
const unsigned int MAX_COMMAND_LINE_SIZE = 256;
const unsigned int MAX_JOBS_PER_LINE = 10;
const unsigned int MAX_SIMPLE_COMMANDS = 10;
const unsigned int MAX_ARGUEMENTS_PER_COMMAND = 10;
const unsigned int MAX_SEQUENCE_OPS = MAX_SIMPLE_COMMANDS;

enum messageIndexes {
	FILEORCOMMAND_NOTFOUND = 0,
	FILEORCOMMAND_NOTEXECUTABLE = 1,
	PATH_NOTFOUND = 2,
	READLINE_NEGATIVE = 3,
	FORK_NEGATIVE = 4,
	EXECVE_RETURNED = 5
};

const char* messages[] = {
	"ERROR: file or command not found!\n",
	"ERROR: file or command not executable!\n",
	"ERROR: PATH variable not found or was empty!\n",
	"ERROR: readline() failed!\n",
	"ERROR: fork() failed!\n",
	"ERROR: execve() failed!\n"
};

enum sequenceOps {
	AND = 1,
	OR = 2
};

enum fileRights {
	FILENOTFOUND = 0,
	FILEEXISTS = 1,
	FILEEXECUTABLE = 2
};

int populatePaths(char* paths[]);
void parseAndExecuteCommands(char* line, size_t count, char* paths[], size_t eachPathVarSize);
int executeSimpleCommand(char* simpleCommand, char* paths[], size_t eachPathVarSize);
int findCommand(char* command, char* path, size_t pathLength, char* paths[], size_t eachPathVarSize);
int isFileExecutable(char* path);
void showPrompt(char* prompt);
int splitIntoJobs(char* cmdline, char* jobs[], size_t count);
int scanSequenceOperators(int seqops[], char* jobstr, size_t count);
int extractSimpleCommands(char* jobstr, char* simple_cmds[], size_t count);
int extractCommandArguments(char* simple_cmd, char** cmd, char* cmdargs[], size_t count);
/* from readline/writeline assignment */
int readline(int, char*, int);
int writeline(int, const char*);

// macros
#define writeError(x)		writeline(2, x)
#define writeStandard(x)	writeline(1, x)
#define getError(x)			messages[x]

int main(void) {
	char* paths[MAX_PATH_VAR_LENGTH];
	char commandLine[MAX_COMMAND_LINE_SIZE];			// For storing user input string
	int bytesRead = 0;
	int eachPathVarSize = 0;

	eachPathVarSize = populatePaths(paths);
	// core loop
	while (1) {
		showPrompt(">> ");	// prompt for command
		bytesRead = readline(0, commandLine, MAX_COMMAND_LINE_SIZE);	// read line and store in commandLine
		if (bytesRead > 0) {	// if anything chars read
			parseAndExecuteCommands(commandLine, bytesRead, paths, eachPathVarSize);
		}
		else {
			if (bytesRead == 0) exit(EXIT_SUCCESS); // if CTRL + D (EOF) was pressed => exit
			writeError(getError(READLINE_NEGATIVE));
		}
	}
	exit(EXIT_SUCCESS);
}

int populatePaths(char* paths[]) {
	char* path = NULL;
	path = getenv("PATH");
	if (path != NULL) {
		char* token;
		char* delim = ":";
		unsigned int i = 0;
		for (token = strtok(path, delim); token != NULL; token = strtok(NULL, delim)) {
			if (i >= MAX_PATH_VAR_LENGTH) break;
			paths[i] = token;
			i++;
		}
		return i;
	}
	else {
		writeError(getError(PATH_NOTFOUND));
		return -1;
	}
}

void parseAndExecuteCommands(char* line, size_t count, char* paths[], size_t eachPathVarSize) {
	char* jobs[MAX_JOBS_PER_LINE];						// For storing 'job' substrings of the input string
	int sequenceOperators[MAX_SEQUENCE_OPS];			// For storing sequence operator codes mapped as ints
	char* simpleCommands[MAX_SIMPLE_COMMANDS];			// For storing simple command strings
	int totalJobs = 0;									// total number of jobs in this line
	int totalSimpleCommands = 0;						// total number of commands (each token seperated by sequence operators)
	int lastResult = 0;									// 0 or 1. 1 if last job failed, 0 if last job succeeded.

	totalJobs = splitIntoJobs(line, jobs, MAX_JOBS_PER_LINE);	// split command line into jobs
	for (int i = 0; i < totalJobs; i++) {
		scanSequenceOperators(sequenceOperators, jobs[i], MAX_SIMPLE_COMMANDS);						// scan for sequence operators
		totalSimpleCommands = extractSimpleCommands(jobs[i], simpleCommands, MAX_SIMPLE_COMMANDS);	// split job into simple commands
		for (int j = 0; j < totalSimpleCommands; j++) {												// for each job, run
			if (j != 0 && ((lastResult && sequenceOperators[j - 1] == AND) || (!lastResult && sequenceOperators[j - 1] == OR))) {
				continue;	// skip next command if last failed and AND or if last succeeded and OR
			}
			lastResult = executeSimpleCommand(simpleCommands[j], paths, eachPathVarSize);
		}
	}
}

int executeSimpleCommand(char* simpleCommand, char* paths[], size_t eachPathVarSize) {
	char* command;											// For storing the command (index 0 of simple command)
	char* commandArguments[MAX_ARGUEMENTS_PER_COMMAND + 2];	// For storing arguments (execve expects first arg to be file executing and must have NULL at end)
	char path[MAX_PATH_LENGTH];								// path to command being executed (will need to include 1 item from paths variable, a '/', and command)
	int pathFound = 0;										// 0 or 1. 0 if path to command was not found, 1 if it was found
	int pid = 0;											// pid used for fork. if < 0, didn't fork, if 0, that process is the child, otherwise parent.

	extractCommandArguments(simpleCommand, &command, commandArguments, MAX_ARGUEMENTS_PER_COMMAND);	// extract the commands and arguements
	pathFound = findCommand(command, path, MAX_PATH_LENGTH, paths, eachPathVarSize);
	if (pathFound == FILENOTFOUND) {
		writeError(getError(FILEORCOMMAND_NOTFOUND));
		return 1;
	}
	else if (pathFound == FILEEXISTS) {
		writeError(getError(FILEORCOMMAND_NOTEXECUTABLE));
		return 1;
	}
	pid = fork();
	if (pid == -1) { // failed to fork
		writeError(getError(FORK_NEGATIVE));
		return 1; // we get to here, then we've failed.
	}
	else if (pid == 0) { // child
		execve(path, commandArguments, NULL);
		writeError(getError(EXECVE_RETURNED));
		exit(EXIT_FAILURE);
	}
	else { // parent
		int wstatus = 0;
		wait(&wstatus); // could use pid in waitpid(pid, &wstatus, 0);
		return WEXITSTATUS(wstatus); // WEXITSTATUS is a macro for getting exit code of process that was waited on.
	}
}

int findCommand(char* command, char* path, size_t pathLength, char* paths[], size_t eachPathVarSize) {
	char* seperator = "/";
	int found = 0;
	int i;
	for (i = 0; command[i] != '\0'; i++) {
		if (command[i] == *seperator) { // check for the seperator
			found = 1;
			break;
		}
	}
	if (found) { // if there are seperators in the command, we'll assume it's an absolute or relative path
		int result = isFileExecutable(command);
		if (result) strncpy(path, command, pathLength);
		return result;
	}
	for(i = 0; i < eachPathVarSize; i++) {
		
		strncpy(path, paths[i], pathLength);					// copy iterated paths string into paths variable
		strncat(path, seperator, pathLength - strlen(path));	// concatonate the seperator '/'
		strncat(path, command, pathLength - strlen(path));		// concatonate the command
		if (isFileExecutable(path) == FILEEXECUTABLE) return FILEEXECUTABLE;
	}
	return FILENOTFOUND;
}

int isFileExecutable(char* path) {
	int currentCheck = 0;
	currentCheck = access(path, F_OK); // exists
	if (currentCheck != 0) return FILENOTFOUND;
	currentCheck = access(path, X_OK); // is executable
	if (currentCheck == 0) return FILEEXECUTABLE; // found
	return FILEEXISTS;
}

void showPrompt(char* prompt) {
	writeStandard(prompt); // print prompt
	fflush(stdout); // allow user to write on same line as prompt
}
int splitIntoJobs(char* cmdline, char* jobs[], size_t count) {
	char* delim = ";";	// jobs delimited by ';'
	char* job;			// current job
	int i;				// counter for total number of jobs and for iteration
	for(i = 0, job = strtok(cmdline, delim); job != NULL && i < count; job = strtok(NULL, delim), i++) {
		jobs[i] = job;	// fill jobs array
	}
	return i;
}
int scanSequenceOperators(int seqops[], char* jobstr, size_t count) {
	char last = '\0';
	char curr = '\0';
	int j = 0;
	for (int i = 0; *(jobstr + i) != '\0' && j < count; i++) { // search string until end
		last = curr;
		curr = *(jobstr + i);
		if ((last == '&' || last == '|') && last == curr) {
			seqops[j++] = last == '|' ? OR : AND;
		}
	}
	return j;
}
int extractSimpleCommands(char* jobstr, char* simple_cmds[], size_t count) {
	char* delims = "&|"; /* can pass in more than one delim and if delim is followed by dupe, dupe is ignored, which is ideal here */
	char* simpleCommand;
	int i;
	for(i = 0, simpleCommand = strtok(jobstr, delims); simpleCommand != NULL && i < count; simpleCommand = strtok(NULL, delims), i++) {
		simple_cmds[i] = simpleCommand;
	}
	return i;
}
int extractCommandArguments(char* simple_cmd, char** cmd, char* cmdargs[], size_t count) {
	char* delim = " ";
	char* piece;
	int i;
	for(i = 0, piece = strtok(simple_cmd, delim); piece != NULL && i < count; piece = strtok(NULL, delim), i++) {
		if (i == 0) {
			(*cmd) = piece;
			cmdargs[i] = piece; // execve expects first to be command
		}
		else cmdargs[i] = piece;
	}
	for (int j = i; j < count; j++) { // replace rest with NULL
		cmdargs[j] = NULL;
	} // the array is actually 1 longer than this function uses, but that value is always null
	return i;
}
int writeline(int fileDescriptor, const char* str) {
	const size_t MAXSTRLEN = 256;
	int i;
	int writeResult;
	unsigned int writeCount = 0;
	/* iterate over str until \0 */
	for (i = 0; str[i] != '\0' && i < MAXSTRLEN; i++) {
		const char* curr_p = &str[i];
		writeResult = write(fileDescriptor, curr_p, 1);
		if (writeResult < 0)
			return writeResult; /* error encounted */
		writeCount++;
	}
	return writeCount;
}
int readline(int fileDescriptor, char* buffer, int bufferSize) {
	int i;
	int readResult;
	for (i = 0; i <= bufferSize; i++) {
		char* curr_p = &buffer[i];
		readResult = read(fileDescriptor, &buffer[i], 1); /* one at a time */
		if (readResult <= 0) /* if zero, EOF, if < 0, error */
			return readResult;
		if (*curr_p == '\n') {
			*curr_p = '\0'; /* swap to c style string */
			break;
		}
	}
	return i + 1;
}
