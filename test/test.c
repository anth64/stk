#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stk.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

volatile sig_atomic_t stop;

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal)
{
	if (signal == CTRL_C_EVENT) {
		stop = 1;
		printf("\nCaught Ctrl+C, shutting down...\n");
		return TRUE;
	}
	return FALSE;
}
#else
void inthand(int signum)
{
	stop = 1;
	printf("\nCaught SIGINT, shutting down...\n");
}
#endif

int main(int argc, char **argv)
{
	unsigned char init_result;
	size_t iterations = 0;

	printf("stk test - CTRL+C to exit\n");

#ifdef _WIN32
	if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
		fprintf(stderr,
			"ERROR: Could not set console control handler\n");
		return EXIT_FAILURE;
	}
#else
	signal(SIGINT, inthand);
#endif

	init_result = stk_init();
	if (init_result != STK_INIT_SUCCESS) {
		fprintf(stderr, "FAIL: stk_init() returned %d\n", init_result);
		return EXIT_FAILURE;
	}

	while (!stop) {
		size_t events = stk_poll();
		if (events > 0)
			printf("Poll: %lu module event(s) detected\n",
			       (unsigned long)events);

		iterations++;
		if (iterations % 5 == 0) {
			printf("Still running... (iteration %lu)\n",
			       (unsigned long)iterations);
		}
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}

	printf("Shutting down stk...\n");
	stk_shutdown();

	return EXIT_SUCCESS;
}
