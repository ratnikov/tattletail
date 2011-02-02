#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#define BUFFER_SIZE 4096
#define MOMMY_OUTPUT_UPDATE      1
#define MOMMY_WINDOW_SIZE_UPDATE 2
#define MOMMY_SESSION_URL        "http://localhost:3000/"
#define MOMMY_UPDATE_URL         "http://localhost:3000/"
#define STOPPED 0
#define RUNNING 1

typedef struct mommy_update {
	int type;
	int cols;
	int rows;
	char * data;
	int length;
	struct mommy_update * next;
} mommy_update_t;

void create_pty();
void spawn_subshell();
void * run_input_thread();
void * run_output_thread();
void * run_mommy_thread();
void on_child_exited();
void on_window_size_changed();
void open_mommy_session();
void close_mommy_session();
void enqueue_mommy_update(mommy_update_t *);
void send_output_to_mommy(char *, int);
void send_window_size_to_mommy(struct winsize *);
void get_terminal_params();
void restore_terminal_params();
inline void guaranteed_write(int, char *, int);
inline void do_or_die(int, char *);

struct termios terminal_params;
int pty_master, pty_slave;
pthread_t input_thread, output_thread, mommy_thread;
int running;
mommy_update_t * mommy_updates = NULL, * mommy_updates_last = NULL;
pthread_mutex_t mommy_updates_mutex;
pthread_cond_t mommy_updates_signal;

int main(int argc, char * argv[])
{
	curl_global_init(CURL_GLOBAL_ALL);

	get_terminal_params();

	open_mommy_session();

	create_pty();

	spawn_subshell();

	running = RUNNING;

	pthread_mutex_init(&mommy_updates_mutex, NULL);
	pthread_cond_init(&mommy_updates_signal, NULL);

	do_or_die(pthread_create(&input_thread, NULL, run_input_thread, NULL), "Unable to create thread");
	do_or_die(pthread_create(&output_thread, NULL, run_output_thread, NULL), "Unable to create thread");
	do_or_die(pthread_create(&mommy_thread, NULL, run_mommy_thread, NULL), "Unable to create thread");

	signal(SIGCHLD, on_child_exited);
	signal(SIGWINCH, on_window_size_changed);

	pthread_join(input_thread, NULL);
	pthread_join(output_thread, NULL);
	pthread_join(mommy_thread, NULL);

	pthread_mutex_destroy(&mommy_updates_mutex);
	pthread_cond_destroy(&mommy_updates_signal);

	close_mommy_session();
}

void create_pty()
{
	struct winsize window_size;

	ioctl(0, TIOCGWINSZ, &window_size);

	do_or_die(openpty(&pty_master, &pty_slave, NULL, &terminal_params, &window_size), "Unable to create a pseudo-terminal");

	send_window_size_to_mommy(&window_size);

	struct termios new_terminal_params;
	new_terminal_params = terminal_params;

	cfmakeraw(&new_terminal_params);
	new_terminal_params.c_lflag &= ~ECHO;
	tcsetattr(0, TCSAFLUSH, &new_terminal_params);
}

void spawn_subshell()
{
	int subshell_pid;

	do_or_die(subshell_pid = fork(), "Unable to fork a subshell");

	if (!subshell_pid)
	{
		char * shell;
		if ((shell = getenv("SHELL")) == NULL)
			shell = "/bin/sh";

		setsid();
		ioctl(pty_slave, TIOCSCTTY, 0);

		close(pty_master);
		dup2(pty_slave, 0);
		dup2(pty_slave, 1);
		dup2(pty_slave, 2);
		close(pty_slave);

		do_or_die(execl(shell, strrchr(shell, '/') + 1, "-i", NULL), "Unable to spawn a subshell");
	}
	else
	{
		close(pty_slave);
	}
}

void * run_input_thread()
{
	register int count;
	char buffer[BUFFER_SIZE];

	while(running && (count = read(0, buffer, BUFFER_SIZE)) > 0)
	{
		guaranteed_write(pty_master, buffer, count);
	}

	pthread_exit(0);
}

void * run_output_thread()
{
	register int count;
	char buffer[BUFFER_SIZE];

	while(running && (count = read(pty_master, buffer, BUFFER_SIZE)) > 0)
	{
		guaranteed_write(1, buffer, count);
		send_output_to_mommy(buffer, count);
	}

	pthread_exit(0);
}

void * run_mommy_thread()
{
	CURL * curl;
	CURLcode result;
	struct curl_httppost * form, * last;
	char * cols, * rows;
	int cols_length, rows_length;
	mommy_update_t * update;

	curl = curl_easy_init();
	// curl_easy_setopt(curl, CURLOPT_PROXY, "\0");
	curl_easy_setopt(curl, CURLOPT_URL, "http://www.postbin.org/13pf22h");

	while(running)
	{

		pthread_mutex_lock(&mommy_updates_mutex);

		if (mommy_updates == NULL)
		{
			pthread_cond_wait(&mommy_updates_signal, &mommy_updates_mutex);
		}

		pthread_mutex_unlock(&mommy_updates_mutex);
		sleep(1);
		pthread_mutex_lock(&mommy_updates_mutex);

		update = mommy_updates;
		mommy_updates = NULL;

		pthread_mutex_unlock(&mommy_updates_mutex);

		for (; update != NULL; update = update->next)
		{
			if (update->type == MOMMY_OUTPUT_UPDATE)
			{
				curl_formadd(&form, &last, CURLFORM_COPYNAME, "content[]",
					                   CURLFORM_COPYCONTENTS, update->data,
					                   CURLFORM_CONTENTSLENGTH, update->length,
					                   CURLFORM_END);
				free(update->data);
			}
			else
			{
				cols_length = asprintf(&cols, "%d", update->cols);
				rows_length = asprintf(&rows, "%d", update->rows);
				curl_formadd(&form, &last, CURLFORM_COPYNAME, "cols[]",
					                   CURLFORM_COPYCONTENTS, cols,
					                   CURLFORM_CONTENTSLENGTH, cols_length,
					                   CURLFORM_END);
				curl_formadd(&form, &last, CURLFORM_COPYNAME, "rows[]",
					                   CURLFORM_COPYCONTENTS, rows,
					                   CURLFORM_CONTENTSLENGTH, rows_length,
					                   CURLFORM_END);
				free(cols);
				free(rows);
			}

			free(update);
		}


		curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
		result = curl_easy_perform(curl);

		curl_formfree(form);
		form = NULL;
		last = NULL;
	}

	curl_easy_cleanup(curl);
	pthread_exit(NULL);
}

void on_child_exited()
{
	running = STOPPED;
	pthread_cancel(input_thread);

	restore_terminal_params();
}

void on_window_size_changed()
{
	struct winsize window_size;

	ioctl(0, TIOCGWINSZ, &window_size);
	ioctl(pty_master, TIOCSWINSZ, &window_size);
	send_window_size_to_mommy(&window_size);
}


void open_mommy_session()
{

}

void close_mommy_session()
{

}

void enqueue_mommy_update(mommy_update_t * update)
{
	pthread_mutex_lock(&mommy_updates_mutex);

	if (mommy_updates == NULL)
	{
		mommy_updates = update;
		mommy_updates_last = update;
	}
	else
	{
		mommy_updates_last->next = update;
		mommy_updates_last = update;
	}

	pthread_cond_signal(&mommy_updates_signal);
	pthread_mutex_unlock(&mommy_updates_mutex);
}

void send_output_to_mommy(char * output, int count)
{
	mommy_update_t * update = malloc(sizeof(mommy_update_t));

	update->type = MOMMY_OUTPUT_UPDATE;
	update->data = malloc(count);
	update->length = count;
	update->next = NULL;
	strncpy(update->data, output, update->length);

	enqueue_mommy_update(update);
}

void send_window_size_to_mommy(struct winsize * window_size)
{
	mommy_update_t * update = malloc(sizeof(mommy_update_t));

	update->type = MOMMY_WINDOW_SIZE_UPDATE;
	update->cols = window_size->ws_col;
	update->rows = window_size->ws_row;
	update->next = NULL;

	enqueue_mommy_update(update);
}

void get_terminal_params()
{
	do_or_die(tcgetattr(0, &terminal_params), "Unable to get terminal settings");
}

void restore_terminal_params()
{
	tcsetattr(0, TCSAFLUSH, &terminal_params);
}

inline void guaranteed_write(int fd, char * buffer, int count)
{
	register int written;

	do
	{
		written = write(fd, buffer, count);

		if (written == count)
		{
			break;
		}
		else
		{
			buffer += written;
			count -= written;
		}
	} while(written >= 0 || errno == EINTR);
}

inline void do_or_die(int result, char * message)
{
	if (result < 0)
	{
		perror(message);

		restore_terminal_params();
		exit(-1);
	}
}

