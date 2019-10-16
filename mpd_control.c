#include <mpd/client.h>

#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <glib.h>

struct worker_meta {
	long long update_interval;
	bool stop;
	int scroll_length;
	int scroll_index;
	char* label;
};

static long long
current_timestamp() {
	struct timeval te;
	gettimeofday(&te, NULL);
	return te.tv_sec*1000LL + te.tv_usec/1000;
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
handle_error(struct mpd_connection *c)
{
	assert(mpd_connection_get_error(c) != MPD_ERROR_SUCCESS);

	fprintf(stderr, "%s\n", mpd_connection_get_error_message(c));
	mpd_connection_free(c);
	return EXIT_FAILURE;
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
print_status(struct mpd_connection *conn, struct worker_meta *meta)
{
	struct mpd_status *status = mpd_run_status(conn);

	if (status == NULL)
		return handle_error(conn);

	const enum mpd_state state = mpd_status_get_state(status);

	char *play_icon = NULL;
	switch (state) {
		case MPD_STATE_PLAY:
			play_icon = "";
			break;
		case MPD_STATE_PAUSE:
			play_icon = "";
			break;
		default:
			printf("\n");
			mpd_status_free(status);
			fflush(stdout);
			return 0;
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

	mpd_status_free(status);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
		return handle_error(conn);

	char artist[256];
	char album[256];
	char title[256];

	struct mpd_song *song = mpd_run_current_song(conn);
	get_tag(song, artist, MPD_TAG_ARTIST);
	get_tag(song, album, MPD_TAG_ALBUM);
	get_tag(song, title, MPD_TAG_TITLE);

	mpd_song_free(song);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
		return handle_error(conn);

	// the full song label. would be cool if this was customizable at runtime
	char full_label[strlen(artist) + strlen(title) + 4];
	sprintf(full_label, "%s - %s", artist, title);

	// compare current label against previous update's label
	if (strcmp(meta->label, full_label) != 0) {
		// The song changed, reset scroll and update meta
		meta->scroll_index = 0;
		strcpy(meta->label, full_label);
	}

	// The label we'll actually print
	// Allocate scroll_length * 4 chars(bytes) since each UTF-8 character may
	// consume up to 4 bytes.
	char label[(meta->scroll_length*4)+1];
	strncpy(label, "", sizeof(label));

	if (g_utf8_strlen(full_label, -1) > meta->scroll_length) {
		// If length of full label > meta->scroll_length, we'll scroll it

		// Pad the text with a separator
		char padded_text[strlen(full_label) + 3 + 1];
		padded_text[sizeof(padded_text)-1] = '\0';

		sprintf(padded_text, "%s | ", full_label);

		scroll_text(padded_text, g_utf8_strlen(padded_text, -1), label,
			&meta->scroll_index, meta->scroll_length);
	} else {
		// Else we'll just print it out normally and reset the scroll index
		meta->scroll_index = 0;
		sprintf(label, "%s - %s", artist, title);
	}

	printf("%s%s %s (-%u:%02u) [%u/%u]\n",
		play_icon, state_icon, label,
		remaining_mins, remaining_secs,
		queue_pos+1, queue_length);

	fflush(stdout);

	return 0;
}

void*
status_loop(void* worker_meta)
{
	struct worker_meta *meta = (struct worker_meta*)worker_meta;
	struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);

	for (;;) {
		if (meta->stop) {
			break;
		}

		long long start = current_timestamp();
		print_status(conn, meta);

		meta->scroll_index++;

		usleep((meta->update_interval - (current_timestamp() - start)) * 1000);
	}

	mpd_connection_free(conn);
	return NULL;
}

enum click_command {
	TOGGLE_PAUSE, NEXT, PREVIOUS, SEEK_FWD, SEEK_BCKWD
};

void
mpd_run_command(struct worker_meta *meta, enum click_command command) {
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
	print_status(conn, meta);
	mpd_connection_free(conn);
}


int main(void) {
	struct worker_meta meta = {950, false, 30, 0, malloc(1024)};

	pthread_t update_thread;
	if(pthread_create(&update_thread, NULL, status_loop, &meta)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	for (;;) {
		char *line = NULL;
		size_t size;

		if (getline(&line, &size, stdin) != -1) {
			if (strcmp(line, "1\n") == 0)	{
				mpd_run_command(&meta, PREVIOUS);
			} else if (strcmp(line, "2\n") == 0) {
				mpd_run_command(&meta, TOGGLE_PAUSE);
			} else if (strcmp(line, "3\n") == 0) {
				mpd_run_command(&meta, NEXT);
			} else if (strcmp(line, "4\n") == 0) {
				mpd_run_command(&meta, SEEK_FWD);
			} else if (strcmp(line, "5\n") == 0) {
				mpd_run_command(&meta, SEEK_BCKWD);
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
