#include <mpd/client.h>

#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <glib.h>
#include <argp.h>

const char *argp_program_version = "mpd_control 0.3.0";
const char *argp_program_bug_address = "<matt-low@zusmail.xyz>";
static char doc[] = "MPD blocket for i3blocks.";
static char args_doc[] = "[OPTION]...";
static struct argp_option options[] = {
	{ "interval", 'i', "interval", 0, "How frequently to update the status line (in seconds). Default: 1"},
	{ "length", 'l', "length", 0, "The length of the shown song label (in characters). Default: 25"},
	{ "step", 's', "step", 0, "How many characters to step the label while scrolling. Default: 3"},
	{ "padding", 'p', "padding", 0, "The characters to append to the label while scrolling. Default: \" | \""},
	{ "format", 'f', "format", 0, "The song label format. Default: '{title} - {artist}'"},
	{ 0 }
};

struct arguments {
	float interval;
	int length;
	int step;
	char* padding;
	char* format;
};
static struct arguments arguments;


static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    switch (key) {
		case 'i':
			if (!arg) break;
			float i = strtof(arg, NULL);
			if (!i || i < 0) {
				return EINVAL;
			}
			arguments->interval = i;
			break;
		case 'l':
			if (!arg) break;
			long l = strtol(arg, NULL, 10);
			if (!l || l < 0) {
				return EINVAL;
			}
			arguments->length = l;
			break;
		case 's':
			if (!arg) break;
			long s = strtol(arg, NULL, 10);
			if (!s || s < 0) {
				return EINVAL;
			}
			arguments->step = s; break;
		case 'p': arguments->padding = arg;
		case 'f': arguments->format = arg;
		case ARGP_KEY_ARG: return 0;
		default: return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };

struct worker_state {
	int scroll_index;
	int song_id;
	char* full_label;
	size_t full_label_length;
	char* padded_label;
	size_t padded_label_length;
};

static long long
current_timestamp() {
	struct timeval te;
	gettimeofday(&te, NULL);
	return te.tv_sec*1000LL + te.tv_usec/1000;
}

static void
str_replace(char *target, size_t target_len, const char *needle, const char *replace)
{
	if (target_len == -1) target_len = strlen(target);

	char *end = &target[target_len];
	size_t needle_len = strlen(needle);
	size_t replace_len = strlen(replace);

	char *pos;
	while ((pos = strstr(target, needle)) != NULL) {
		// Shift portion after needle to its position after replace
		memmove(pos + replace_len, pos + needle_len, (end - (pos + needle_len)) + 1);
		end += replace_len - needle_len;

		// Replace needle with repl string
		memcpy(pos, replace, replace_len);
	}
}

static void
scroll_text(const char *text_to_scroll, const size_t text_len, char *buffer,
			int *index, const int max_length) {
	if (*index - text_len > 0) *index %= text_len;

	char* first_char = g_utf8_offset_to_pointer(text_to_scroll, *index);
	const int overflow = text_len - *index - max_length;
	if (overflow <= 0) {
		g_utf8_strncpy(buffer, first_char, text_len-*index);
		g_utf8_strncpy(g_utf8_offset_to_pointer(buffer, text_len-*index),
			text_to_scroll, max_length-(text_len-*index));
	} else {
		g_utf8_strncpy(buffer, first_char, max_length);
	}
}

static int
handle_error(struct mpd_connection *conn, bool renew)
{
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		if (!renew) {
			fprintf(stderr, "%s\n", mpd_connection_get_error_message(conn));
		}
		if (!mpd_connection_clear_error(conn)) {
			if (renew) {
				mpd_connection_free(conn);
				conn = mpd_connection_new(NULL, 0, 0);
				return handle_error(conn, false);
			}
			return EXIT_FAILURE;
		} else {
			return 0;
		}
	}
	return 0;
}

static void
get_tag(struct mpd_song *song, char *tag, enum mpd_tag_type type)
{
	const char *value;
	unsigned i = 0;
	while ((value = mpd_song_get_tag(song, type, i++)) != NULL) {
		strcpy(tag, value);
		break;
	}
}

static int
print_status(struct mpd_connection *conn, struct worker_state *state)
{
	struct mpd_status *status = mpd_run_status(conn);

	if (status == NULL)
		return handle_error(conn, false);

	char *play_icon = NULL;
	switch (mpd_status_get_state(status)) {
		case MPD_STATE_PLAY:
			play_icon = "";
			break;
		case MPD_STATE_PAUSE:
			play_icon = "";
			break;
		default:
			state->scroll_index = 0;
			state->song_id = -1;
			printf("\n");
			mpd_status_free(status);
			fflush(stdout);
			return 1;
	}

	const bool repeat = mpd_status_get_repeat(status);
	const bool random = mpd_status_get_random(status);

	char state_icon[16];
	strcpy(state_icon, "");
	if (repeat)
		strcat(state_icon, " ");
	if (random)
		strcat(state_icon, " ");

	const unsigned queue_pos = mpd_status_get_song_pos(status);
	const unsigned queue_length = mpd_status_get_queue_length(status);
	const unsigned elapsed = mpd_status_get_elapsed_time(status);
	const unsigned remaining = mpd_status_get_total_time(status) - elapsed;
	const unsigned remaining_mins = remaining / 60;
	const unsigned remaining_secs = remaining % 60;
	const int song_id = mpd_status_get_song_id(status);

	mpd_status_free(status);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
		return handle_error(conn, false);

	if (song_id != state->song_id) {
		state->song_id = song_id;

		char artist[256];
		char album[256];
		char title[256];

		struct mpd_song *song = mpd_run_current_song(conn);
		get_tag(song, artist, MPD_TAG_ARTIST);
		get_tag(song, album, MPD_TAG_ALBUM);
		get_tag(song, title, MPD_TAG_TITLE);
		mpd_song_free(song);
		if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
			return handle_error(conn, false);

		// The song changed, so let's update the full label...
		state->full_label = strcpy(state->full_label, arguments.format);
		str_replace(state->full_label, -1, "{title}", title);
		str_replace(state->full_label, -1, "{album}", album);
		str_replace(state->full_label, -1, "{artist}", artist);

		state->full_label_length = g_utf8_strlen(state->full_label, -1);

		// ... and the padded label (if the full label is long enough) ...
		if (state->full_label_length > arguments.length) {
			sprintf(state->padded_label, "%s%s", state->full_label, arguments.padding);
			state->padded_label_length = g_utf8_strlen(state->padded_label, -1);
		}
		// ... and reset the scroll index
		state->scroll_index = 0;
	}

	// The label we'll actually print
	char *label = NULL;

	if (state->full_label_length > arguments.length) {
		// If length of full label > scroll length, we'll scroll it

		// Allocate enough space for the buffer
		// Up to 4 bytes per character because utf8.
		label = alloca((arguments.length*4)+1);
		label[0] = '\0';

		scroll_text(state->padded_label, state->padded_label_length, label,
			&state->scroll_index, arguments.length);
	} else {
		// Else we'll just point label directly to the full label
		label = state->full_label;
	}

	printf("%s%s %s (-%u:%02u) [%u/%u]\n",
		play_icon, state_icon, label,
		remaining_mins, remaining_secs,
		queue_pos+1, queue_length);

	fflush(stdout);

	return 0;
}

static inline
long long max(int a, int b) {
	return a >= b? a : b;
}

void*
status_loop(void* worker_state)
{
	struct worker_state *state = (struct worker_state*)worker_state;
	struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);

	for (;;) {
		long long start = current_timestamp();

		if (handle_error(conn, true) == 0 && print_status(conn, state) == 0) {
			state->scroll_index += arguments.step;
		}

		long long elapsed = current_timestamp() - start;
		usleep(max(0, ((arguments.interval*1000) - elapsed) * 1000));
	}

	mpd_connection_free(conn);
	return NULL;
}

enum click_command {
	TOGGLE_PAUSE, NEXT, PREVIOUS, SEEK_FWD, SEEK_BCKWD
};

void
mpd_run_command(struct worker_state *state, enum click_command command) {
	struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);
	switch (command) {
		case TOGGLE_PAUSE:
			mpd_run_toggle_pause(conn);
			break;
		case NEXT:
			mpd_run_next(conn);
			break;
		case PREVIOUS:
			mpd_run_previous(conn);
			break;
		case SEEK_FWD:
			mpd_run_seek_current(conn, 3, true);
			break;
		case SEEK_BCKWD:
			mpd_run_seek_current(conn, -3, true);
			break;
	}
	print_status(conn, state);
	mpd_connection_free(conn);
}

int main(int argc, char *argv[]) {
	arguments.interval = 1.0f;
	arguments.length = 25;
	arguments.step = 3;
	arguments.padding = " | ";
	arguments.format = "{title} - {artist}";

	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	struct worker_state state = {0, -1, malloc(1024), 0, malloc(1024), 0};

	struct sigaction new_actn, old_actn;
	new_actn.sa_handler = SIG_IGN;
	sigemptyset(&new_actn.sa_mask);
	new_actn.sa_flags = 0;
	sigaction(SIGPIPE, &new_actn, &old_actn);

	pthread_t update_thread;
	if(pthread_create(&update_thread, NULL, status_loop, &state)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	for (;;) {
		char *line = NULL;
		size_t size;

		if (getline(&line, &size, stdin) != -1) {
			if (strcmp(line, "1\n") == 0)	{
				mpd_run_command(&state, PREVIOUS);
			} else if (strcmp(line, "2\n") == 0) {
				mpd_run_command(&state, TOGGLE_PAUSE);
			} else if (strcmp(line, "3\n") == 0) {
				mpd_run_command(&state, NEXT);
			} else if (strcmp(line, "4\n") == 0) {
				mpd_run_command(&state, SEEK_FWD);
			} else if (strcmp(line, "5\n") == 0) {
				mpd_run_command(&state, SEEK_BCKWD);
			}

			free(line);
		}
	}

	if(pthread_join(update_thread, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}

	return 0;
}
